#ifndef AV_COMMOEN_H
#define AV_COMMOEN_H
#include <QtGlobal>
#include <QDebug>
#include <chrono>
#include <memory>
#include <mutex>
#include "AV_Queue.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "libavutil/error.h"
}

using AVPacketPtr = std::shared_ptr<AVPacket>;
using AVFramePtr  = std::shared_ptr<AVFrame>;

//gdigrab 桌面采集的实际输出格式。采集缓冲池的行跨度和编码器入参都由它派生，
//避免同一块内存在采集端与编码器之间出现两种格式口径。
constexpr AVPixelFormat kCapturePixelFormat = AV_PIX_FMT_BGRA;

//采集与编码统一的目标帧率。采集端的 gdigrab framerate、编码器的 time_base
//以及约 1 秒的 GOP 都由它派生，避免出现多套帧率口径。
constexpr qint32 kTargetFramerate = 60;

//采集时间轴统一口径：以本会话起点为零点，把采集完成时刻量化到 1/目标帧率 的时间格。
//GDI 与 WGC 都用它生成 VideoFrame::sequence，使序号真正落在单调时钟上。之前 GDI 用
//序号自增，隐含假设「采集速率 == 目标帧率」；采集跟不上时（GDI 在 60Hz 目标下只有 ~45fps）
//时间轴每真实一秒只走 0.75 秒，与真实时间对不上。PTS 由序号差派生，这里就是时间轴的根。
//四舍五入到最近格；两帧过近仍可能落进同一格，调用方须再用 max(seq, 上次+1) 兜底。
inline quint64 MakeSequence(std::chrono::steady_clock::time_point now,
                            std::chrono::steady_clock::time_point start)
{
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    return static_cast<quint64>((us * kTargetFramerate + 500000) / 1000000);
}

typedef struct VIDEOCONFIG
{
    quint32 width;
    quint32 height;
    quint32 bitrate;
    quint32 framerate;
    quint32 gop;
    AVPixelFormat format;
}VideoConfig;

typedef struct AUDIOCONFIG
{
    quint32 channels;
    quint32 samplerate;
    quint32 bitrate;
    AVSampleFormat format;
}AudioConfig;

struct AVConfig
{
    VideoConfig video;
    AudioConfig audio;
};

// 单帧编码路径的分段耗时，单位为微秒。阶段一低频统计使用，阶段三验收后移除。
// convertUs：把采集帧转成编码器像素格式的耗时（CPU 路径是 swscale，GPU 路径是 VideoProcessorBlt）。
// lockWaitUs：等共享 D3D11 立即上下文锁的耗时，只有 GPU 路径非零；用来判断采集与编码是否在互相排队。
struct VideoEncodeTiming
{
    quint64 convertUs = 0;
    quint64 lockWaitUs = 0;
    quint64 encodeUs = 0;
};

// 解码线程与播放线程之间的视频帧队列，只保留最新一帧。
// 解码快于渲染时，旧方案会让 video_queue_ 越堆越长，画面离最新状态越来越远；
// 这里放入新帧时直接替换尚未取走的旧帧，让显示端永远只看到最近解码出的画面。
// 音频不适用该策略，继续使用普通 FIFO 的 AVQueue。
// empty/size 会被播放线程跨线程调用，因此与 push/pop 走同一把锁。
class VideoFrameQueue
{
public:
    // 解码线程：放入一帧。若已有未取走的帧则将其丢弃并返回 true，
    // 调用方据此统计“帧队列替换数”。
    bool push(const AVFramePtr& frame)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool replaced = static_cast<bool>(frame_);
        frame_ = frame;
        return replaced;
    }

    // 播放线程：取走最新一帧，队列为空时返回 false。
    bool pop(AVFramePtr& frame)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(!frame_)
        {
            return false;
        }
        frame = frame_;
        frame_.reset();
        return true;
    }

    bool empty() const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return !frame_;
    }

    // 当前长度，只会是 0 或 1。
    int size() const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return frame_ ? 1 : 0;
    }

    void clear()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        frame_.reset();
    }

private:
    mutable std::mutex mutex_;
    AVFramePtr frame_ = nullptr;
};

struct AVContext
{
public:
    //音频相关参数//
    int32_t audio_sample_rate;
    int32_t audio_channels_layout;
    AVRational audio_src_timebase;
    AVRational audio_dst_timebase;
    AVSampleFormat audio_fmt;
    double audioDuration;
    AVQueue<AVFramePtr> audio_queue_;
    //视频相关参数
    int32_t video_width;
    int32_t video_height;
    AVRational video_src_timebase;
    AVRational video_dst_timebase;
    AVPixelFormat video_fmt;
    double videoDuration;
    // 视频专用：只保留最新一帧，避免解码快于渲染时持续积压
    VideoFrameQueue video_queue_;

    int avMediatype_ = 0;
};


class EncodBase
{
public:
    EncodBase():is_initialzed_(false),codec_(nullptr),codecContext_(nullptr){config_ = {};}
    virtual ~EncodBase(){if(codecContext_)avcodec_free_context(&codecContext_);}
    EncodBase(const EncodBase&) = delete;
    EncodBase& operator=(const EncodBase&) = delete;
public:
    virtual bool Open(AVConfig& config) = 0;
    virtual void Close() = 0;
    AVCodecContext* GetAVCodecContext() const
    {return codecContext_;}
protected:
    bool is_initialzed_ = false;
    AVConfig config_;
    AVCodec* codec_;
    AVCodecContext *codecContext_ = nullptr;
};

class DecodBase
{
public:
    DecodBase():is_initial_(false),video_index_(-1),audio_index_(-1),codec_(nullptr),codecCtx_(nullptr){config_ = {};}
    virtual ~DecodBase(){if(codecCtx_){avcodec_free_context(&codecCtx_);};}
    DecodBase(const DecodBase&) = delete;
    DecodBase& operator=(const DecodBase&) = delete;
    AVCodecContext* GetAVCodecContext() const
    {return codecCtx_;}
protected:
    bool is_initial_;
    std::mutex mutex_;
    qint32 video_index_;
    qint32 audio_index_;
    AVConfig config_;
    AVCodec* codec_;
    AVCodecContext *codecCtx_;
};

#endif //AV_COMMOEN_H
