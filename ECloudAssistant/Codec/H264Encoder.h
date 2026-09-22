#ifndef H264_ENCODER_H
#define H264_ENCODER_H
#include <QtGlobal>
#include <vector>
#include "AV_Common.h"
class VideoEncoder;
class H264Encoder
{
public:
    H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;
    ~H264Encoder();
public:
    bool OPen(qint32 width,qint32 height,qint32 framerate,qint32 bitrate,qint32 format);
    void Close();
    //pts 为必传的单调递增帧序号，沿调用链原样交给编码器，不做换算
    qint32 Encode(const quint8* rgba_buffer,quint32 width,quint32 height,qint64 pts,
                  std::vector<quint8>& out_frame,VideoEncodeTiming* timing = nullptr);
    qint32 GetSequenceParams(quint8* out_buffer, qint32 out_buffer_size);
private:
    bool IsKeyFrame(AVPacketPtr pkt);
private:
    AVConfig config_;
    std::unique_ptr<VideoEncoder> h264_encoder_;
};
#endif // H264_ENCODER_H
