#ifndef VIDEOPIPELINESTATS_H
#define VIDEOPIPELINESTATS_H
#include <QtGlobal>
#include <chrono>

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

    // 记录一帧编码完成。sequence 来自采集端，用于识别重复编码和跳帧。
    void OnFrameEncoded(quint64 sequence,quint64 waitUs,quint64 convertUs,quint64 encodeUs);

    // 距上次汇总满一秒时输出一行并开始下一周期。
    // capturedSequence 是采集端当前产出的帧序号，用来计算本周期实际采集帧数。
    void ReportIfDue(std::chrono::steady_clock::time_point now,quint64 capturedSequence);
private:
    //只清本周期累加量，不动 started_ 和序号比较状态
    void ResetInterval();

    static constexpr quint64 kReportIntervalUs = 1000000;
    static constexpr double kMicrosecondsPerSecond = 1000000.0;

    bool started_;
    std::chrono::steady_clock::time_point intervalBegin_;
    quint64 lastCapturedSequence_;

    quint64 encodedFrames_;
    quint64 duplicateFrames_;
    quint64 skippedFrames_;
    quint64 waitUs_;
    quint64 waitMaxUs_;
    quint64 convertUs_;
    quint64 encodeUs_;

    bool hasEncodedSequence_;
    quint64 lastEncodedSequence_;
};
#endif // VIDEOPIPELINESTATS_H
