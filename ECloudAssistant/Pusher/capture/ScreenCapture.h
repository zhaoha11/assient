#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QtGlobal>
#include "VideoFrame.h"

class D3D11SharedContext;

// 采集后端向外提供统一的 VideoFrame：可以是 CPU BGRA，也可以是 GPU 纹理。
// 帧的底层数据所有权由 VideoFrame 自带的共享指针保证；取用方持有该帧期间数据不会被覆盖。
class ScreenCapture
{
public:
    // 采集输出形态：CPU 读回（默认，兼容现有软件编码链路）或 GPU 纹理。
    enum class CaptureOutput { CpuReadback, GpuTexture };

    virtual ~ScreenCapture() = default;
    virtual bool Init(qint64 display_index = 0) = 0;
    virtual bool WaitLatestFrame(VideoFrame& frame) = 0;
    virtual void RequestStop() = 0;
    virtual bool Close() = 0;
    virtual quint32 GetWidth() const = 0;
    virtual quint32 GetHeight() const = 0;
    // 已产出的真实采集帧总数，供低频统计计采集帧率；
    // 与 VideoFrame::sequence 无关：后者是时钟量化后的时间格序号，用于派生 PTS。
    virtual quint64 GetCapturedFrames() const = 0;

    // 输出形态能力：不支持的实现直接沿用默认值（GDI 即如此）。
    virtual bool SupportsGpuOutput() const { return false; }
    virtual bool SetOutput(CaptureOutput) { return false; }

    // 采集与硬件编码共用同一个 D3D11 device 时由上层注入；不支持共享的实现沿用默认值。
    // 必须在 Init 之前调用，采集会话建立时才会用上。
    virtual bool SetExternalDevice(D3D11SharedContext*) { return false; }
};

#endif // SCREENCAPTURE_H
