#ifndef VIDEOFRAME_H
#define VIDEOFRAME_H

#include <QtGlobal>
#include <chrono>
#include <memory>
#include <vector>

// 采集层与编码层之间的中立帧抽象。
// 这里刻意不包含任何平台头（windows.h / d3d11.h）：GPU 帧通过抽象句柄暴露原生资源，
// 具体类型只在采集后端和未来的硬件编码器里解释。

enum class VideoFrameKind { Cpu, Gpu };

enum class VideoPixelFormat { Bgra8, Nv12, Unknown };

// GPU 帧句柄。由后端实现持有原生资源并保证其生命周期；
// 公共头不感知 D3D11，取用方自行 static_cast（如 ID3D11Texture2D*）。
class IGpuVideoFrame
{
public:
    virtual ~IGpuVideoFrame() = default;
    virtual quint32 width() const = 0;
    virtual quint32 height() const = 0;
    virtual VideoPixelFormat format() const = 0;
    virtual void* nativeTexture() const = 0;   // 例：ID3D11Texture2D*
    virtual void* nativeDevice() const = 0;    // 例：ID3D11Device*
};

// CPU 帧视图。owner 保证 data 在使用期间有效；
// GDI 的三缓冲由内部保证、owner 为空，data 指向当轮 front。
struct CpuFrameView
{
    std::shared_ptr<const std::vector<quint8>> owner;
    const quint8* data = nullptr;
    quint32 stride = 0;
};

struct VideoFrame
{
    VideoFrameKind kind = VideoFrameKind::Cpu;
    CpuFrameView cpu;                     // kind == Cpu 时有效
    std::shared_ptr<IGpuVideoFrame> gpu;  // kind == Gpu 时有效，持有底层的生命周期
    quint32 width = 0;
    quint32 height = 0;
    quint64 sequence = 0;
    std::chrono::steady_clock::time_point capturedAt;
};

#endif // VIDEOFRAME_H
