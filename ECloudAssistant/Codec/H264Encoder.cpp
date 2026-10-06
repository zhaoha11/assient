#include "H264Encoder.h"
#include "VideoFrame.h"
#include "SoftwareVideoEncoder.h"
#include "HardwareVideoEncoder.h"
#include <QDebug>

// 硬件编码器候选，按优先级尝试；FFmpeg 列出名字不等于本机能打开，真正的判定是 Open 成功
static const char* const kHardwareEncoderCandidates[] = {"h264_nvenc","h264_qsv","h264_amf"};

H264Encoder::H264Encoder()
    :config_{}
    ,h264_encoder_(nullptr)
{
    //默认先放一个软编占位，保证成员非空；OPen 时按需替换
    h264_encoder_.reset(new SoftwareVideoEncoder());
}

H264Encoder::~H264Encoder()
{
    Close();
}

bool H264Encoder::OPen(qint32 width, qint32 height, qint32 framerate, qint32 bitrate, qint32 format,
                       VideoEncoderKind kind, D3D11SharedContext* shared)
{
    //初始化编码器
    config_.video.width = width;
    config_.video.height = height;
    config_.video.framerate = framerate;
    config_.video.bitrate = bitrate * 1000;
    config_.video.gop = framerate;
    config_.video.format = (AVPixelFormat)format;

    //编码器选择只在会话建立时做一次：硬编不可用就回退软编，运行中不再切换
    if(kind == VideoEncoderKind::Hardware)
    {
        // GPU 直通模式：采集端已经在输出 GPU 纹理，编码器必须成对地开成 GPU 模式。
        // 这里不能回退到软编或「硬编 CPU 模式」——那会变成「GPU 帧喂 CPU 编码器」的错配，
        // 直接整体失败，交回上层退到 CPU 读回路径。
        if(shared)
        {
            std::unique_ptr<HardwareVideoEncoder> hardware(
                new HardwareVideoEncoder("h264_nvenc",shared));
            if(!hardware->Open(config_))
            {
                qWarning() << "[ENCODE] nvenc gpu passthrough open failed";
                return false;
            }
            h264_encoder_ = std::move(hardware);
            qInfo() << "[ENCODE] active encoder =" << h264_encoder_->EncoderName()
                    << "(gpu passthrough)";
            return true;
        }

        for(const char* const name : kHardwareEncoderCandidates)
        {
            std::unique_ptr<HardwareVideoEncoder> hardware(new HardwareVideoEncoder(name));
            if(hardware->Open(config_))
            {
                h264_encoder_ = std::move(hardware);
                qInfo() << "[ENCODE] active encoder =" << h264_encoder_->EncoderName() << "(cpu input)";
                return true;
            }
            qWarning() << "[ENCODE] hardware encoder open failed, trying next:" << name;
        }
        qWarning() << "[ENCODE] no hardware encoder available, falling back to software";
    }

    h264_encoder_.reset(new SoftwareVideoEncoder());
    if(!h264_encoder_->Open(config_))
    {
        return false;
    }
    qInfo() << "[ENCODE] active encoder =" << h264_encoder_->EncoderName();
    return true;
}

void H264Encoder::Close()
{
    h264_encoder_->Close();
}

qint32 H264Encoder::Encode(const quint8 *rgba_buffer, quint32 width, quint32 height, qint64 pts,
                           std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
    //编码264
    out_frame.clear();
    //开始编码
    AVPacketPtr pkt = h264_encoder_->Encode(rgba_buffer,width,height,pts,timing);
    if(!pkt)
    {
        //编码失败
        return -1;
    }
    return EmitPacket(pkt,out_frame);
}

// 关键帧前置 extradata（SPS/PPS），再拼上裸流：[编码信息 + 264 裸流]
qint32 H264Encoder::EmitPacket(AVPacketPtr pkt,std::vector<quint8>& out_frame)
{
    if(!pkt)
    {
        out_frame.clear();
        return -1;
    }
    int frame_size = 0;
    int max_out_size = config_.video.width * config_.video.height * 4;//设大一点 因为这个编码数据不会大于这个原始数据RGBA
    std::shared_ptr<quint8> out_buffer(new quint8[max_out_size],std::default_delete<quint8[]>());
    //判断是否是关键帧 如果是关键帧需要在264前面添加编码信息
    if(IsKeyFrame(pkt))
    {
        AVCodecContext* codecContext = h264_encoder_->GetAVCodecContext();
        //编码信息放到包头去解析
        memcpy(out_buffer.get(),codecContext->extradata,codecContext->extradata_size);
        frame_size += codecContext->extradata_size;
    }
    memcpy(out_buffer.get() + frame_size,pkt->data,pkt->size);
    frame_size += pkt->size;

    //需要将数据传出去out_frame
    if(frame_size > 0)
    {
        out_frame.resize(frame_size);
        out_frame.assign(out_buffer.get(),out_buffer.get() + frame_size);
        return frame_size;
    }
    return 0;
}

qint32 H264Encoder::EncodeFrame(const VideoFrame &frame, qint64 pts,
                                std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
    if(frame.kind == VideoFrameKind::Gpu)
    {
        if(!frame.gpu)
        {
            out_frame.clear();
            return -1;
        }
        return EncodeGpuFrame(*frame.gpu,frame.width,frame.height,pts,out_frame,timing);
    }
    return EncodeCpuFrame(frame.cpu,frame.width,frame.height,pts,out_frame,timing);
}

qint32 H264Encoder::EncodeCpuFrame(const CpuFrameView &cpu, quint32 width, quint32 height, qint64 pts,
                                   std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
    if(!cpu.data)
    {
        out_frame.clear();
        return -1;
    }
    // 采集侧输出的 CPU 帧是紧凑 BGRA（stride == width*4），Encode 内部即按此跨度拷贝
    return Encode(cpu.data,width,height,pts,out_frame,timing);
}

// 把采集侧的 GPU 纹理交给编码器：只有硬件编码器在 GPU 模式下能消费，其余返回失败。
qint32 H264Encoder::EncodeGpuFrame(IGpuVideoFrame &gpu, quint32 width, quint32 height, qint64 pts,
                                   std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
    Q_UNUSED(width);
    Q_UNUSED(height);
    out_frame.clear();
    if(!h264_encoder_)
    {
        return -1;
    }
    AVPacketPtr pkt = h264_encoder_->EncodeGpuFrame(gpu,pts,timing);
    if(!pkt)
    {
        return -1;
    }
    return EmitPacket(pkt,out_frame);
}

bool H264Encoder::HasFatalError() const
{
    return h264_encoder_ && h264_encoder_->HasFatalError();
}

qint32 H264Encoder::GetSequenceParams(quint8 *out_buffer, qint32 out_buffer_size)
{
    //获取编码参数
    quint32 size = 0;
    if(!h264_encoder_->GetAVCodecContext())
    {
        return -1;
    }
    AVCodecContext* codecContxt = h264_encoder_->GetAVCodecContext();
    size = codecContxt->extradata_size;
    memcpy(out_buffer,codecContxt->extradata,codecContxt->extradata_size);
    return size;
}

bool H264Encoder::IsKeyFrame(AVPacketPtr pkt)
{
    //判断是否为关键帧
    return pkt->flags & AV_PKT_FLAG_KEY;
}
