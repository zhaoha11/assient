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
}

H264Encoder::~H264Encoder()
{
    Close();
}

bool H264Encoder::OPen(qint32 width, qint32 height, qint32 framerate, qint32 bitrate, qint32 format,
                       VideoEncoderKind kind, D3D11SharedContext* shared)
{
    Close();
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
        Close();
        return false;
    }
    qInfo() << "[ENCODE] active encoder =" << h264_encoder_->EncoderName();
    return true;
}

void H264Encoder::Close()
{
    h264_encoder_.reset();
}

// 关键帧前置 extradata（SPS/PPS），再拼上裸流：[编码信息 + 264 裸流]
qint32 H264Encoder::EmitPacket(AVPacketPtr pkt,std::vector<quint8>& out_frame)
{
    if(!pkt)
    {
        out_frame.clear();
        return -1;
    }
    const AVCodecContext* codecContext = h264_encoder_->GetAVCodecContext();
    const int header_size = (pkt->flags & AV_PKT_FLAG_KEY) ? codecContext->extradata_size : 0;
    const int frame_size = header_size + pkt->size;
    out_frame.resize(frame_size);
    if(header_size > 0)
    {
        memcpy(out_frame.data(),codecContext->extradata,header_size);
    }
    if(pkt->size > 0)
    {
        memcpy(out_frame.data() + header_size,pkt->data,pkt->size);
    }
    return frame_size;
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
        return EncodeGpuFrame(*frame.gpu,pts,out_frame,timing);
    }
    return EncodeCpuFrame(frame.cpu,frame.width,frame.height,pts,out_frame,timing);
}

qint32 H264Encoder::EncodeCpuFrame(const CpuFrameView &cpu, quint32 width, quint32 height, qint64 pts,
                                   std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
    out_frame.clear();
    if(!h264_encoder_ || !cpu.data)
    {
        return -1;
    }
    return EmitPacket(h264_encoder_->Encode(cpu,width,height,pts,timing),out_frame);
}

// 把采集侧的 GPU 纹理交给编码器：只有硬件编码器在 GPU 模式下能消费，其余返回失败。
qint32 H264Encoder::EncodeGpuFrame(IGpuVideoFrame &gpu, qint64 pts,
                                   std::vector<quint8> &out_frame, VideoEncodeTiming *timing)
{
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
    if(!h264_encoder_ || !out_buffer)
    {
        return -1;
    }
    const AVCodecContext* codecContext = h264_encoder_->GetAVCodecContext();
    if(!codecContext || !codecContext->extradata || codecContext->extradata_size <= 0 ||
       out_buffer_size < codecContext->extradata_size)
    {
        return -1;
    }
    memcpy(out_buffer,codecContext->extradata,codecContext->extradata_size);
    return codecContext->extradata_size;
}
