#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H
#include <memory>
#include "AV_Common.h"
#include "VideoConvert.h"

class IGpuVideoFrame;
struct CpuFrameView;

// H.264 编码器基类：把软编/硬编共享的部分集中在这里——FFmpeg 上下文创建、
// CPU BGRA→目标像素格式的转换、send/receive、以及 SPS/PPS extradata 生成。
// 两者的差异只在子类里体现：用哪个编码器、输出什么像素格式、设哪些私有选项。
class VideoEncoder : public EncodBase
{
public:
    VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    virtual ~VideoEncoder();
public:
    virtual bool Open(AVConfig& video_config) override;
    virtual void Close()override;
    //pts 必须是显式传入的单调递增序号：编码器时间基为 1/帧率，序号差即帧间隔。
    // CPU 缓冲仅在本次同步转换期间借用，编码器只持有转换后的独立帧。
    virtual AVPacketPtr Encode(const CpuFrameView& cpu,quint32 width,quint32 height,qint64 pts,
                               VideoEncodeTiming* timing = nullptr);
    // GPU 纹理路径：只有硬件编码器在 GPU 模式下实现，其余（含软编）沿用默认返回 nullptr。
    virtual AVPacketPtr EncodeGpuFrame(IGpuVideoFrame& gpu,qint64 pts,
                                       VideoEncodeTiming* timing = nullptr);
    // 运行中不可恢复的编码错误，供上层决定是否降级重建会话
    virtual bool HasFatalError() const { return false; }
    // 实际生效的编码器名，供启动日志区分软/硬编
    virtual const char* EncoderName() const = 0;
protected:
    // 子类返回要打开的编码器：软编按 ID 找，硬编按名字找
    virtual AVCodec* FindCodec() = 0;
    // 子类设置编码器专属参数：输出像素格式、档位、私有选项、可选硬件设备。
    // 返回 false 表示该编码器不可用，Open 会整体失败交给上层回退。
    virtual bool ConfigureCodec() = 0;
private:
    //上一次用于建立转换器的输入尺寸，与编码器尺寸无关（编码器尺寸可能被截成偶数）
    quint32 sourceWidth_;
    quint32 sourceHeight_;
    AVFramePtr  rgba_frame_;
    AVPacketPtr h264_packet_;
    std::unique_ptr<VideoConverter> converter_;
protected:
    // 硬件帧链路的 device / frames 上下文，仅 GPU 模式子类填充，Base::Close 统一释放
    AVBufferRef* hwDeviceRef_ = nullptr;
    AVBufferRef* hwFramesRef_ = nullptr;
    // 临时埋点：量编码器输出延迟 D（帧）。送帧前 +1、收到包后 -1，
    // 稳定值即「已送帧数 − 已收包数」= D。仅 D 变化时打印，正常应在前几帧后静默。
    void NoteFrameSent();
    void NotePacketReceived(qint64 inPts,const AVPacket* pkt);
private:
    qint64 pendingFrames_ = 0;
    qint64 lastDelay_ = -1;
};
#endif // VIDEO_ENCODER_H
