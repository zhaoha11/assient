#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include "VideoSource.h"

class D3D11SharedContext;

// 屏幕采集的公共接口：在 VideoSource 之上补充屏幕专属的初始化和 GPU 配置。
// 摄像头不继承本类，因此这些屏幕专属能力不会被带到摄像头路径。
class ScreenCapture : public VideoSource
{
public:
    // 采集输出形态：CPU 读回（默认，兼容现有软件编码链路）或 GPU 纹理。
    enum class CaptureOutput { CpuReadback, GpuTexture };

    virtual ~ScreenCapture() = default;
    virtual bool Init(qint64 display_index = 0) = 0;

    // 不支持指定输出形态的实现直接返回 false（GDI 即如此）。
    virtual bool SetOutput(CaptureOutput) { return false; }

    // 采集与硬件编码共用同一个 D3D11 device 时由上层注入；不支持共享的实现沿用默认值。
    // 必须在 Init 之前调用，采集会话建立时才会用上。
    virtual bool SetExternalDevice(D3D11SharedContext*) { return false; }
};

#endif // SCREENCAPTURE_H
