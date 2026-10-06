#include "VideoPipelineStats.h"
#include <QDebug>
#include <QString>

namespace
{
quint64 AverageUs(quint64 totalUs,quint64 frames)
{
    return frames > 0 ? totalUs / frames : 0;
}
}

VideoPipelineStats::VideoPipelineStats()
{
    Reset();
}

void VideoPipelineStats::Reset()
{
    ResetInterval();

    started_ = false;
    intervalBegin_ = std::chrono::steady_clock::time_point();
    lastCapturedFrames_ = 0;

    hasEncodedSequence_ = false;
    lastEncodedSequence_ = 0;
}

void VideoPipelineStats::ResetInterval()
{
    encodedFrames_ = 0;
    duplicateFrames_ = 0;
    skippedFrames_ = 0;
    waitUs_ = 0;
    waitMaxUs_ = 0;
    convertUs_ = 0;
    encodeUs_ = 0;
    encodedBytes_ = 0;
}

void VideoPipelineStats::OnFrameEncoded(quint64 sequence, quint64 waitUs, quint64 convertUs, quint64 encodeUs, quint64 frameBytes)
{
    if(!kEnabled)
    {
        return;
    }

    if(hasEncodedSequence_)
    {
        if(sequence == lastEncodedSequence_)
        {
            //同一张采集帧被再次编码
            ++duplicateFrames_;
        }
        else if(sequence > lastEncodedSequence_ + 1)
        {
            //编码线程落后于采集线程，中间的画面已被覆盖
            skippedFrames_ += sequence - lastEncodedSequence_ - 1;
        }
    }
    hasEncodedSequence_ = true;
    lastEncodedSequence_ = sequence;

    ++encodedFrames_;
    waitUs_ += waitUs;
    convertUs_ += convertUs;
    encodeUs_ += encodeUs;
    encodedBytes_ += frameBytes;
    if(waitUs > waitMaxUs_)
    {
        waitMaxUs_ = waitUs;
    }
}

void VideoPipelineStats::ReportIfDue(std::chrono::steady_clock::time_point now, quint64 capturedFrames)
{
    if(!kEnabled)
    {
        return;
    }

    if(!started_)
    {
        //第一次调用只用来对齐采集端帧计数，不输出半个周期的不完整数据。
        //本帧的编码记录发生在 intervalBegin_ 之前，必须一并清掉，
        //否则第一个窗口会多算一帧编码、少算一帧采集。
        started_ = true;
        intervalBegin_ = now;
        lastCapturedFrames_ = capturedFrames;
        ResetInterval();
        return;
    }

    const quint64 elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - intervalBegin_).count();
    if(elapsedUs < kReportIntervalUs)
    {
        return;
    }

    //采集帧数取真实帧计数差，与时间格序号无关（后者在 60Hz 目标下恒等于 elapsed×60）
    const quint64 framesThisWindow = capturedFrames > lastCapturedFrames_
                                     ? capturedFrames - lastCapturedFrames_ : 0;

    //本窗口编码后的 H.264 码流码率：字节 ×8 转比特，再按窗口实际时长折算 kbps
    const double bitrateKbps = encodedBytes_ * 8.0 * kMicrosecondsPerSecond / elapsedUs / 1000.0;

    qInfo() << QString("[PIPE-STATS] 采集帧率 = %1 编码帧率 = %2 采集帧数 = %3 编码帧数 = %4 重复 = %5 跳帧 = %6"
                       " 等待均值us = %7 等待峰值us = %8 转换均值us = %9 编码均值us = %10 码率kbps = %11 序号 = %12")
                   .arg(framesThisWindow * kMicrosecondsPerSecond / elapsedUs, 0, 'f', 1)
                   .arg(encodedFrames_ * kMicrosecondsPerSecond / elapsedUs, 0, 'f', 1)
                   .arg(framesThisWindow)
                   .arg(encodedFrames_)
                   .arg(duplicateFrames_)
                   .arg(skippedFrames_)
                   .arg(AverageUs(waitUs_, encodedFrames_))
                   .arg(waitMaxUs_)
                   .arg(AverageUs(convertUs_, encodedFrames_))
                   .arg(AverageUs(encodeUs_, encodedFrames_))
                   .arg(bitrateKbps, 0, 'f', 1)
                   .arg(lastEncodedSequence_);

    intervalBegin_ = now;
    lastCapturedFrames_ = capturedFrames;
    ResetInterval();
}
