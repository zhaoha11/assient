#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QtGlobal>
#include <chrono>
#include <memory>
#include <vector>

// 两种后端都输出紧凑的 BGRA，stride = width * 4。
// GDI 的 data 有效期到下一次 WaitLatestFrame()/Close()；WGC 的 owner 保持读回缓冲有效。
struct CaptureFrameView
{
    std::shared_ptr<const std::vector<quint8>> owner;
    const quint8* data = nullptr;
    quint32 width = 0;
    quint32 height = 0;
    quint32 stride = 0;
    quint32 size = 0;
    quint64 sequence = 0;
    std::chrono::steady_clock::time_point capturedAt;
};

class ScreenCapture
{
public:
    virtual ~ScreenCapture() = default;
    virtual bool Init(qint64 display_index = 0) = 0;
    virtual bool WaitLatestFrame(CaptureFrameView& frame) = 0;
    virtual void RequestStop() = 0;
    virtual bool Close() = 0;
    virtual quint32 GetWidth() const = 0;
    virtual quint32 GetHeight() const = 0;
    virtual quint64 GetCaptureSequence() const = 0;
};

#endif // SCREENCAPTURE_H
