#ifndef VIDEOPIPELINESTATS_H
#define VIDEOPIPELINESTATS_H
#include <QtGlobal>
#include <chrono>
#include "StatsWindow.h"

// 采集到编码链路的低频统计，用于阶段一建立延迟基线。
// 约定：除 Reset() 外，所有接口只在视频编码线程调用，内部状态不需要加锁。
// 阶段三验收完成后整体删除本文件及其调用点。
class VideoPipelineStats
{
public:
    // 置为 false 即可完全关闭统计，不产生任何输出。
    static constexpr bool kEnabled = true;

    VideoPipelineStats();

    // 重新开始一次统计周期，首次建立推流和再次建立推流时各调用一次。
    void Reset();

    // 记录一帧编码完成。sequence 是采集端的时钟量化时间格序号（用于识别跳帧，单位是时间格）；
    // frameBytes 是本帧编码后的码流字节数（可能为 0，表示编码器还在缓冲），用于统计码率。
    void OnFrameEncoded(quint64 sequence,quint64 waitUs,quint64 convertUs,quint64 encodeUs,quint64 frameBytes);

    // 记录一次从最新帧槽位成功取到帧及其阻塞时长。
    // 与「等待均值us」是两回事：后者是采集完成到编码开始（含正常等待），
    // 这里只量 WaitLatestFrame 本身的阻塞，60fps 下约 16.7ms 属完全正常。
    void NoteFramePulled(quint64 blockUs);

    // 记录一次编码线程等共享 D3D11 立即上下文锁的时长，只有 GPU 路径非零。
    // 判据：几十~几百 us 属正常串行；若常态几毫秒到十几毫秒，说明采集与编码在互相排队，
    // 那才是要重新设计 context 使用方式的理由。
    void NoteEncodeLockWait(quint64 waitUs);

    // 距上次汇总满一秒时输出一行并开始下一周期。
    // 源帧率/发布帧率/丢旧帧只由采集侧的 [CAP-STATS] 报，这里不重复。
    void ReportIfDue(std::chrono::steady_clock::time_point now);
private:
    //只清本周期累加量，不动 started_ 和序号比较状态
    void ResetInterval();

    static constexpr quint64 kReportIntervalUs = 1000000;
    static constexpr double kMicrosecondsPerSecond = 1000000.0;

    bool started_;
    std::chrono::steady_clock::time_point intervalBegin_;

    quint64 encodedFrames_;
    quint64 skippedFrames_;
    quint64 waitUs_;
    quint64 waitMaxUs_;
    quint64 convertUs_;
    quint64 encodeUs_;
    quint64 encodedBytes_;

    UsWindow pullBlockUs_;
    UsWindow encodeLockWaitUs_;

    bool hasEncodedSequence_;
    quint64 lastEncodedSequence_;
};
#endif // VIDEOPIPELINESTATS_H
