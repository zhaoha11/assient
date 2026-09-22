#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H
#include <memory>
#include "AV_Common.h"
#include "VideoConvert.h"
class VideoEncoder : public EncodBase
{
public:
    VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    ~VideoEncoder();
public:
    virtual bool Open(AVConfig& video_config) override;
    virtual void Close()override;
    //pts 必须是显式传入的单调递增序号：编码器时间基为 1/帧率，序号差即帧间隔。
    //不给默认值是为了杜绝「调用方漏传、编码器静默收到 0」这类无符号哨兵语义。
    virtual AVPacketPtr Encode(const quint8* data,quint32 width,quint32 height,qint64 pts,
                               VideoEncodeTiming* timing = nullptr);
private:
    quint32 width_;
    quint32 height_;
    //上一次用于建立转换器的输入尺寸，与编码器尺寸无关（编码器尺寸可能被截成偶数）
    quint32 sourceWidth_;
    quint32 sourceHeight_;
    AVFramePtr  rgba_frame_;
    AVPacketPtr h264_packet_;
    std::unique_ptr<VideoConverter> converter_;
};
#endif // VIDEO_ENCODER_H
