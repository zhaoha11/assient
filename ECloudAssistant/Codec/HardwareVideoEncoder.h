#ifndef HARDWARE_VIDEO_ENCODER_H
#define HARDWARE_VIDEO_ENCODER_H
#include <atomic>
#include <memory>
#include <string>
#include "VideoEncoder.h"

class D3D11SharedContext;
class IGpuVideoFrame;

// 硬件 H.264：按名字打开 nvenc/qsv/amf。
// 传入共享 D3D11 设备且编码器为 nvenc 时进入 GPU 模式：输入是采集侧产出的 D3D11 纹理，
// 由 D3D11 VideoProcessor 在 GPU 内转成 NV12 直接交给编码器，全程不读回 CPU。
// 否则沿用 CPU 模式：输入 CPU BGRA，由基类的 swscale 转换器转出 NV12。
class HardwareVideoEncoder : public VideoEncoder
{
public:
    explicit HardwareVideoEncoder(const char* codecName,
                                  D3D11SharedContext* shared = nullptr);
    ~HardwareVideoEncoder() override;

    const char* EncoderName() const override { return codecName_.c_str(); }
    AVPacketPtr EncodeGpuFrame(IGpuVideoFrame& gpu,qint64 pts,
                               VideoEncodeTiming* timing = nullptr) override;
    bool HasFatalError() const override { return fatalError_.load(); }

protected:
    AVCodec* FindCodec() override;
    bool ConfigureCodec() override;
    void Close() override;

private:
    // 各厂商编码器的私有选项 key/value 不同，写错只会被忽略或回落默认值，不会让 open 崩溃
    void SetEncoderOptions();
    // GPU 模式：建 FFmpeg 硬件帧上下文（复用共享 device），并置 pix_fmt/hw_frames_ctx
    bool ConfigureGpuFrames();

private:
    std::string codecName_;
    D3D11SharedContext* shared_ = nullptr;
    bool gpuMode_ = false;
    AVPacketPtr gpuPacket_;
    std::atomic<bool> fatalError_{false};
    // D3D11 与硬件帧相关的原生类型都藏在 cpp 里，公共头不引入 d3d11.h
    struct GpuEncodeResources;
    std::unique_ptr<GpuEncodeResources> gpu_;
};
#endif // HARDWARE_VIDEO_ENCODER_H
