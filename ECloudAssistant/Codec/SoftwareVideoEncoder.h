#ifndef SOFTWARE_VIDEO_ENCODER_H
#define SOFTWARE_VIDEO_ENCODER_H
#include "VideoEncoder.h"

// 软件 H.264（libx264）：输出 YUV420P，极速/零延迟档，是默认且最稳的路径。
class SoftwareVideoEncoder : public VideoEncoder
{
public:
    const char* EncoderName() const override { return "software(libx264)"; }
protected:
    AVCodec* FindCodec() override;
    bool ConfigureCodec() override;
};
#endif // SOFTWARE_VIDEO_ENCODER_H
