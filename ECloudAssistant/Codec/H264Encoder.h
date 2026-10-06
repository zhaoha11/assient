#ifndef H264_ENCODER_H
#define H264_ENCODER_H
#include <QtGlobal>
#include <vector>
#include "AV_Common.h"
class VideoEncoder;
class D3D11SharedContext;
struct VideoFrame;
struct CpuFrameView;
class IGpuVideoFrame;

// 编码器种类：会话级一次决定，运行中不再切换。
// Software 为默认且最稳；Hardware 会尝试 nvenc/qsv/amf，全不可用时回退软件。
enum class VideoEncoderKind { Software, Hardware };

class H264Encoder
{
public:
    H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;
    ~H264Encoder();
public:
    // shared 非空且 kind==Hardware 时，nvenc 会走 GPU 直通（输入 D3D11 纹理），
    // 因此 GPU 采集输出与硬编必须在同一路径里成对启用，由上层负责配对。
    bool OPen(qint32 width,qint32 height,qint32 framerate,qint32 bitrate,qint32 format,
              VideoEncoderKind kind = VideoEncoderKind::Software,
              D3D11SharedContext* shared = nullptr);
    void Close();
    //pts 为必传的单调递增帧序号，沿调用链原样交给编码器，不做换算
    qint32 Encode(const quint8* rgba_buffer,quint32 width,quint32 height,qint64 pts,
                  std::vector<quint8>& out_frame,VideoEncodeTiming* timing = nullptr);
    // 统一入口：按帧类型分发到 CPU / GPU 路径，调用方不必关心采集后端。
    qint32 EncodeFrame(const VideoFrame& frame,qint64 pts,
                       std::vector<quint8>& out_frame,VideoEncodeTiming* timing = nullptr);
    // CPU BGRA 走现有软件编码路径。
    qint32 EncodeCpuFrame(const CpuFrameView& cpu,quint32 width,quint32 height,qint64 pts,
                          std::vector<quint8>& out_frame,VideoEncodeTiming* timing = nullptr);
    // GPU 纹理路径：本阶段仅占位，硬件编码留待下一阶段接入。
    qint32 EncodeGpuFrame(IGpuVideoFrame& gpu,quint32 width,quint32 height,qint64 pts,
                          std::vector<quint8>& out_frame,VideoEncodeTiming* timing = nullptr);
    qint32 GetSequenceParams(quint8* out_buffer, qint32 out_buffer_size);
    // 运行中不可恢复的编码错误：上层据此降级重建会话（软编路径）
    bool HasFatalError() const;
private:
    bool IsKeyFrame(AVPacketPtr pkt);
    // 关键帧前置 extradata 后拷入 out_frame；CPU / GPU 两条编码路径共用
    qint32 EmitPacket(AVPacketPtr pkt,std::vector<quint8>& out_frame);
private:
    AVConfig config_;
    std::unique_ptr<VideoEncoder> h264_encoder_;
};
#endif // H264_ENCODER_H
