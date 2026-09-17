#ifndef GDISCREENCAPTURE_H
#define GDISCREENCAPTURE_H
#include <QThread>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
struct AVFrame;
struct AVPacket;
struct AVInputFormat;
struct AVCodecContext;
struct AVFormatContext;

// 编码线程持有的只读视图。data 指向三缓冲中的 front，缓冲区由本类持有。
// 有效期到下一次 WaitLatestFrame() 或 Close() 为止；调用方不得保存、释放或异步使用。
struct CaptureFrameView
{
    const quint8* data = nullptr;
    quint32 width = 0;
    quint32 height = 0;
    quint32 stride = 0;
    quint32 size = 0;
    quint64 sequence = 0;
    std::chrono::steady_clock::time_point capturedAt;
};

class GDIScreenCapture : public QThread
{
public:
    GDIScreenCapture();
    GDIScreenCapture(const GDIScreenCapture&) = delete;
    GDIScreenCapture& operator=(const GDIScreenCapture&) = delete;
    virtual ~GDIScreenCapture();
public:
    virtual quint32 GetWidth() const;
    virtual quint32 GetHeight() const;
    virtual bool Init(qint64 display_index = 0);
    // 阻塞到有新画面可用，返回 false 表示已停止。
    virtual bool WaitLatestFrame(CaptureFrameView& frame);
    // 只置停止标志并唤醒等待者：幂等、不 join、不释放缓冲池。
    // 必须在消费者线程 join 之前调用，否则消费者会永久阻塞在条件变量上。
    virtual void RequestStop();
    // 停止采集线程并释放三块缓冲区。不能在消费者仍持有 CaptureFrameView 时调用。
    virtual bool Close();
    // 已产出的采集帧总数，与 CaptureFrameView::sequence 同源，供低频统计读取。
    virtual quint64 GetCaptureSequence() const;
protected:
    virtual void run() override;
private:
    // 三缓冲中的一块。采集线程独占写 back，编码线程独占读 front，middle 是交接缓冲。
    struct CaptureFrameBuffer
    {
        std::vector<quint8> data;
        quint32 width = 0;
        quint32 height = 0;
        quint32 stride = 0;
        quint32 validBytes = 0;
        quint64 sequence = 0;
        std::chrono::steady_clock::time_point capturedAt;
    };

    void StopCapture();
    bool GetOneFrame();
    bool Decode(AVFrame* av_frame,AVPacket* av_packet);
private:
    std::atomic<bool> stop_;
    std::atomic<bool> is_initialzed_;
    quint32 width_;
    quint32 height_;
    std::atomic<quint64> capture_sequence_;
    qint64  video_index_;
    qint64  framerate_;
    bool    format_logged_;
    bool    mismatch_warned_;
    AVInputFormat* input_format_;
    AVCodecContext* codec_context_;
    AVFormatContext* format_context_;
    std::shared_ptr<AVFrame> av_frame_;
    std::shared_ptr<AVPacket> av_packet_;
    // 以下成员共同维护三缓冲的交接：索引两两不等且覆盖 {0,1,2}，
    // 采集只动 {back, middle}，编码只动 {front, middle}，因此锁外写入的目标始终私有。
    std::array<CaptureFrameBuffer,3> frameBuffers_;
    std::size_t frontIndex_;
    std::size_t middleIndex_;
    std::size_t backIndex_;
    std::mutex frameMutex_;
    std::condition_variable frameReady_;
    bool hasNewFrame_;
    bool stopped_;
};
#endif // GDISCREENCAPTURE_H
