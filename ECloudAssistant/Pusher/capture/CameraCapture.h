#ifndef CAMERACAPTURE_H
#define CAMERACAPTURE_H
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "VideoSource.h"

struct AVFrame;
struct AVPacket;
struct AVInputFormat;
struct AVCodecContext;
struct AVFormatContext;
struct SwsContext;

// 默认摄像头采集：FFmpeg dshow 打开本机首个视频设备，解码后统一转 BGRA，
// 以 CPU VideoFrame 发布给编码线程。第一版使用设备默认模式与软件编码。
//
// dshow 的设备打开、读取、释放全部在同一个工作线程内完成：FFmpeg 的 dshow 后端
// 在 read_header 里 CoInitialize(0)、在 read_close 里 CoUninitialize，跨线程调用会
// 破坏 COM 套间约定，因此这里由工作线程独自持有整条设备生命周期。
class CameraCapture : public VideoSource
{
public:
    CameraCapture();
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;
    virtual ~CameraCapture();
public:
    // 启动工作线程并在其内打开设备，返回时宽高已就绪；失败（无设备/被占用/超时）返回 false。
    bool Init();
    quint32 GetWidth() const override { return width_.load(); }
    quint32 GetHeight() const override { return height_.load(); }
    // 阻塞到有新画面可用，返回 false 表示已停止。始终输出 CPU BGRA。
    bool WaitLatestFrame(VideoFrame& frame) override;
    // 只置停止标志并唤醒等待者：幂等、不 join、不释放缓冲。必须在消费者线程 join 之前调用。
    void RequestStop() override;
    // 停止工作线程并释放设备与缓冲。不能在消费者仍持有上一帧 VideoFrame 时调用。
    bool Close() override;
    // 源帧数：实际解码出的采集帧总数。摄像头每解出一帧就发布，与发布帧数同值。
    quint64 GetCapturedFrames() const override { return capturedFrames_.load(); }
    quint64 GetPublishedFrames() const override { return publishedFrames_.load(); }
private:
    // 三缓冲中的一块。采集线程独占写 back，编码线程独占读 front，middle 是交接缓冲。
    struct CaptureFrameBuffer
    {
        std::vector<quint8> data;
        quint32 width = 0;
        quint32 height = 0;
        quint32 stride = 0;
        quint64 sequence = 0;
        std::chrono::steady_clock::time_point capturedAt;
    };

    // dshow 的 read_packet 不轮询该回调（阻塞在 WaitForMultipleObjects(INFINITE)），
    // 它只在打开/探测阶段对 FFmpeg 内部的读取循环有意义，正常停靠每帧到达后的检查兜底。
    static int InterruptCb(void* opaque);
    void Run();
    std::string PickDefaultDevice();
    bool OpenDevice();
    void ReleaseDevice();
    bool GetOneFrame();
    bool Decode(AVFrame* av_frame,AVPacket* av_packet);
private:
    std::thread worker_;
    std::atomic<bool> stop_{false};
    // 设备已打开、缓冲已分配，WaitLatestFrame 方能返回帧
    std::atomic<bool> ready_{false};
    std::atomic<quint32> width_{0};
    std::atomic<quint32> height_{0};
    std::atomic<quint64> capturedFrames_{0};
    std::atomic<quint64> publishedFrames_{0};
    // 设备生命周期由工作线程独占，下列成员只在工作线程内访问
    std::string device_name_;
    qint32 declared_framerate_ = 0;
    bool format_logged_ = false;
    bool mismatch_warned_ = false;
    SwsContext* sws_context_ = nullptr;
    // sws_context_ 对应的源参数；用于自行判断是否需要重建（见 Decode 内说明）
    int sws_src_width_ = 0;
    int sws_src_height_ = 0;
    int sws_src_format_ = -1;
    AVInputFormat* input_format_ = nullptr;
    AVCodecContext* codec_context_ = nullptr;
    AVFormatContext* format_context_ = nullptr;
    qint64 video_index_ = -1;
    std::shared_ptr<AVFrame> av_frame_;
    std::shared_ptr<AVPacket> av_packet_;
    //时间轴：会话起点 + 上一次发布的时间格序号，仅在采集线程内读写
    std::chrono::steady_clock::time_point sessionStart_;
    quint64 lastSequence_ = 0;
    // 三缓冲交接：索引两两不等且覆盖 {0,1,2}，采集只动 {back,middle}，编码只动 {front,middle}
    std::array<CaptureFrameBuffer,3> frameBuffers_;
    std::size_t frontIndex_ = 0;
    std::size_t middleIndex_ = 1;
    std::size_t backIndex_ = 2;
    std::mutex frameMutex_;
    std::condition_variable frameReady_;
    std::condition_variable initReady_;
    bool hasNewFrame_ = false;
    bool stopped_ = true;
    bool initDone_ = false;
    bool initOk_ = false;
};
#endif // CAMERACAPTURE_H
