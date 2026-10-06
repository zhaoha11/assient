#include "VideoEncoder.h"
#include "VideoFrame.h"
#include <QDebug>
#include <chrono>
#include <limits>

extern "C"
{
    #include <libavutil/rational.h>
    #include <libavutil/error.h>
    #include<libavutil/opt.h>
}

VideoEncoder::VideoEncoder()
    :sourceWidth_(0)
    ,sourceHeight_(0)
    ,rgba_frame_(nullptr)
    ,h264_packet_(nullptr)
    ,converter_(nullptr)
{
    //创建AVframe avpacket
    rgba_frame_.reset(av_frame_alloc(),[](AVFrame* ptr){av_frame_free(&ptr);});
    h264_packet_.reset(av_packet_alloc(),[](AVPacket* ptr){av_packet_free(&ptr);});
}

VideoEncoder::~VideoEncoder()
{
    Close();
}

bool VideoEncoder::Open(AVConfig &video_config)
{
    //初始化
    if(is_initialzed_)
    {
        Close();
    }

    config_ = video_config;

    // Open 会被「候选编码器依次尝试」多次调用，这里先释放上一次的上下文；
    // 否则每次重试都新建一份，只有 EncodBase 析构时才回收，中间全部泄漏。
    if(codecContext_)
    {
        avcodec_free_context(&codecContext_);
    }

    //查找编码器：具体用哪个交给子类决定
    codec_ = const_cast<AVCodec*>(FindCodec());
    if(!codec_)
    {
        Close();
        return false;
    }

    //创建编码器上下文
    codecContext_ = avcodec_alloc_context3(codec_);
    if(!codecContext_)
    {
        Close();
        return false;
    }

    //软硬编共享的基础参数；像素格式/档位/私有选项由 ConfigureCodec 决定
    codecContext_->width = config_.video.width;
    codecContext_->height = config_.video.height;
    codecContext_->time_base = {1,(qint32)config_.video.framerate};//帧率倒数
    codecContext_->framerate = {(qint32)config_.video.framerate,1};
    //gop_size 是帧数不是秒数：只有由帧率派生，关键帧间隔才会稳定在约 1 秒，
    //否则改帧率会无声地改变 I 帧的时间间隔
    codecContext_->gop_size = config_.video.gop;
    codecContext_->max_b_frames = 0;//降低延迟
    codecContext_->bit_rate = config_.video.bitrate;
    //还需要设置全局头
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    //子类专属配置，失败即整体失败，由上层决定是否回退到别的编码器
    if(!ConfigureCodec())
    {
        Close();
        return false;
    }

    //打开编码器
    if(avcodec_open2(codecContext_,codec_,NULL) != 0)
    {
        Close();
        return false;
    }

    is_initialzed_ = true;
    return true;
}

void VideoEncoder::Close()
{
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    is_initialzed_ = false;
    // 每次会话重新计数，D 的测量从新一帧开始
    pendingFrames_ = 0;
    lastDelay_ = -1;
    // 先解 frames 再解 device：frames 上下文持有 device 引用
    if(hwFramesRef_)
    {
        av_buffer_unref(&hwFramesRef_);
    }
    if(hwDeviceRef_)
    {
        av_buffer_unref(&hwDeviceRef_);
    }
    if(converter_)
    {
        converter_->Close();
        converter_.reset();
    }
}

AVPacketPtr VideoEncoder::Encode(const CpuFrameView& cpu, quint32 width, quint32 height,
                                 qint64 pts, VideoEncodeTiming* timing)
{
    //开始编码
    if(!is_initialzed_ || !cpu.data || width == 0 || height == 0 || cpu.stride < width * 4 ||
       cpu.stride > static_cast<quint32>((std::numeric_limits<int>::max)()))
    {
        return nullptr;
    }

    //比较的必须是输入尺寸：编码器尺寸可能被截成偶数，跟采集尺寸不同
    if(sourceWidth_ != width || sourceHeight_ != height || !converter_)
    {
        converter_.reset(new VideoConverter());
        //初始化视频转换器：输出格式取编码器实际要求的 pix_fmt，软编 YUV420P、硬编 NV12
        if(!converter_->Open(width,height,(AVPixelFormat)config_.video.format,
                              codecContext_->width,codecContext_->height,codecContext_->pix_fmt))
        {
            //初始化失败
            converter_.reset();
            return nullptr;
        }

        sourceWidth_ = width;
        sourceHeight_ = height;
    }

    // 只借用采集缓冲作为 swscale 输入；转换同步完成后解除借用，不分配或复制 BGRA。
    rgba_frame_->width = width;
    rgba_frame_->height = height;
    rgba_frame_->format = config_.video.format;
    rgba_frame_->data[0] = const_cast<quint8*>(cpu.data);
    rgba_frame_->linesize[0] = static_cast<int>(cpu.stride);
    const std::chrono::steady_clock::time_point convertBegin = std::chrono::steady_clock::now();
    AVFramePtr out_frame = nullptr;
    const int converted = converter_->Convert(rgba_frame_,out_frame);
    rgba_frame_->data[0] = nullptr;
    rgba_frame_->linesize[0] = 0;
    if(converted <= 0)
    {
        return nullptr;
    }
    if(timing)
    {
        timing->convertUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - convertBegin).count();
    }

    //更新out_frame参数
    out_frame->pts = pts;
    out_frame->pict_type = AV_PICTURE_TYPE_NONE;

    const std::chrono::steady_clock::time_point encodeBegin = std::chrono::steady_clock::now();
    NoteFrameSent();
    const int sendResult = avcodec_send_frame(codecContext_,out_frame.get());
    if(sendResult < 0)
    {
        return nullptr;
    }
    //我们再去接收这个值
    int ret = avcodec_receive_packet(codecContext_,h264_packet_.get());
    if(timing)
    {
        timing->encodeUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - encodeBegin).count();
    }
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return nullptr;
    }
    else if(ret < 0)
    {
        return nullptr;
    }
    NotePacketReceived(out_frame->pts,h264_packet_.get());
    return h264_packet_;
}

// 送帧计数 +1。落在 send_frame 之前：无论这一帧最终有没有立刻产出包，它都已经进入编码器。
void VideoEncoder::NoteFrameSent()
{
    ++pendingFrames_;
}

// 收到包后 -1 并打印当前 D。D = 已送帧数 − 已收包数，稳定下来就是编码器的输出延迟帧数。
// 只在 D 变化时打印：固定流水线深度会很快静默，若 D 一路增长则说明是无界堆积。
void VideoEncoder::NotePacketReceived(qint64 inPts,const AVPacket* pkt)
{
    if(pendingFrames_ > 0) --pendingFrames_;
    if(pendingFrames_ == lastDelay_) return;
    lastDelay_ = pendingFrames_;
    const qint64 pktPts = pkt ? pkt->pts : 0;
    const qint64 pktDts = pkt ? pkt->dts : 0;
    qInfo() << "[LAT-D] encoder delay =" << pendingFrames_
            << "inPts =" << inPts << "pktPts =" << pktPts << "pktDts =" << pktDts;
}

// 默认不实现 GPU 路径：只有硬件编码器在 GPU 模式下 override。
AVPacketPtr VideoEncoder::EncodeGpuFrame(IGpuVideoFrame& gpu,qint64 pts,VideoEncodeTiming* timing)
{
    Q_UNUSED(gpu);
    Q_UNUSED(pts);
    Q_UNUSED(timing);
    return nullptr;
}
