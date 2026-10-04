#ifndef VIDEOPULLSTATS_H
#define VIDEOPULLSTATS_H
#include <QtGlobal>
#include <atomic>

// 拉流端视频链路 demux→解码→videoPlay→Repaint/paintGL 的每秒汇总统计，
// 用于阶段二只加统计不改播放逻辑地定位延迟积压。与推流端 VideoPipelineStats
// 同一套约定：kEnabled 置为 false 即完全关闭；阶段五验收完成后整体删除
// 本文件及其调用点。
// 调用线程共四个：demux 线程、H264 解码线程、videoPlay 线程、GUI 线程，
// 因此所有状态都是原子量；每秒汇总输出只发生在 videoPlay 线程，保证不刷屏。
class VideoPullStats
{
public:
    // 置为 false 即可完全关闭统计，不产生任何输出。
    static constexpr bool kEnabled = true;

    // demux 线程：一个视频包已压入 H264 解码器输入队列，queueLen 为入队后的队列长度。
    static void OnVideoPacketQueued(int queueLen);

    // demux 线程：记录每次 av_read_frame 调用耗时，以及成功返回的视频包到达间隔。
    static void OnReadFrame(qint64 readUs,qint64 arrivalUs,bool videoPacket);

    // 解码线程：一帧解码并转换完成已压入 avContext 的 video_queue_，queueLen 同上。
    static void OnFrameDecoded(int queueLen);

    // videoPlay 线程：已从 video_queue_ 取出一帧并发送 sig_repaint。
    static void OnFrameFetched();

    // GUI 线程：Repaint() 入口调用。emitUs 是 videoPlay 发送 sig_repaint 的时刻
    // （steady_clock 微秒），返回本次进入渲染的时刻，供 OnPaintScheduled 继续传递。
    static qint64 OnRepaint(qint64 emitUs);

    // GUI 线程：Repaint() 调用 update() 之后调用，repaintUs 为 OnRepaint 返回值，
    // 用于统计 Repaint() 到 paintGL() 的滞后。
    static void OnPaintScheduled(qint64 repaintUs);

    // GUI 线程：paintGL() 入口调用。
    static void OnPaintGL();

    // videoPlay 线程：每轮循环调用，距上次汇总满一秒时输出一行并开始下一周期。
    // packetQueueLen/frameQueueLen 是输出时刻对压缩包队列和解码帧队列
    // "当前长度"的安全抽样（走 AVQueue 加锁的 size()）。
    static void ReportIfDue(int packetQueueLen,int frameQueueLen);

    // 再次开始拉流时调用，清零全部累计状态。
    static void Reset();
private:
    static void UpdateMax(std::atomic<qint64>& maxVal,qint64 value);
    static void ResetInterval();

    static qint64 NowUs();

    static constexpr quint64 kReportIntervalUs = 1000000;

    static std::atomic<bool> started_;
    static std::atomic<qint64> intervalBeginUs_;

    static std::atomic<qint64> readPackets_;      //av_read_frame 读到的视频包数
    static std::atomic<qint64> readCalls_;        //本窗口 av_read_frame 调用次数
    static std::atomic<qint64> readCallUs_;       //调用耗时累计
    static std::atomic<qint64> readCallMaxUs_;
    static std::atomic<qint64> videoGaps_;        //相邻视频包间隔的样本数
    static std::atomic<qint64> videoGapUs_;
    static std::atomic<qint64> videoGapMaxUs_;
    static std::atomic<qint64> lastVideoPacketUs_; //跨窗口保留，换流时清零
    static std::atomic<qint64> packetQueueMax_;   //压缩包队列本窗口峰值
    static std::atomic<qint64> decodedFrames_;    //解码并转换输出的帧数
    static std::atomic<qint64> frameQueueMax_;    //解码帧队列本窗口峰值
    static std::atomic<qint64> fetchedFrames_;    //videoPlay 取帧/发信号帧数
    static std::atomic<qint64> repaints_;         //Repaint() 执行次数
    static std::atomic<qint64> paintGLs_;         //paintGL() 执行次数
    static std::atomic<qint64> signalWaitUs_;     //sig_repaint 到 Repaint 的累计等待
    static std::atomic<qint64> signalWaitMaxUs_;
    static std::atomic<qint64> paintLagUs_;       //Repaint 到 paintGL 的累计滞后
    static std::atomic<qint64> paintLagMaxUs_;
    static std::atomic<qint64> paintPendingUs_;   //最近一次进入 Repaint 的时刻，0 表示无未决重绘
};
#endif // VIDEOPULLSTATS_H
