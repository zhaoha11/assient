#include "VideoPullStats.h"
#include <QDebug>
#include <QString>
#include <chrono>

std::atomic<bool> VideoPullStats::started_{false};
std::atomic<qint64> VideoPullStats::intervalBeginUs_{0};
std::atomic<qint64> VideoPullStats::readPackets_{0};
std::atomic<qint64> VideoPullStats::readCalls_{0};
std::atomic<qint64> VideoPullStats::readCallUs_{0};
std::atomic<qint64> VideoPullStats::readCallMaxUs_{0};
std::atomic<qint64> VideoPullStats::videoGaps_{0};
std::atomic<qint64> VideoPullStats::videoGapUs_{0};
std::atomic<qint64> VideoPullStats::videoGapMaxUs_{0};
std::atomic<qint64> VideoPullStats::lastVideoPacketUs_{0};
std::atomic<qint64> VideoPullStats::packetQueueMax_{0};
std::atomic<qint64> VideoPullStats::decodedFrames_{0};
std::atomic<qint64> VideoPullStats::frameQueueMax_{0};
std::atomic<qint64> VideoPullStats::fetchedFrames_{0};
std::atomic<qint64> VideoPullStats::repaints_{0};
std::atomic<qint64> VideoPullStats::paintGLs_{0};
std::atomic<qint64> VideoPullStats::signalWaitUs_{0};
std::atomic<qint64> VideoPullStats::signalWaitMaxUs_{0};
std::atomic<qint64> VideoPullStats::paintLagUs_{0};
std::atomic<qint64> VideoPullStats::paintLagMaxUs_{0};
std::atomic<qint64> VideoPullStats::paintPendingUs_{0};

qint64 VideoPullStats::NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
}

void VideoPullStats::UpdateMax(std::atomic<qint64>& maxVal, qint64 value)
{
    qint64 cur = maxVal.load(std::memory_order_relaxed);
    while(value > cur &&
          !maxVal.compare_exchange_weak(cur,value,std::memory_order_relaxed))
    {
    }
}

void VideoPullStats::ResetInterval()
{
    readPackets_.store(0,std::memory_order_relaxed);
    readCalls_.store(0,std::memory_order_relaxed);
    readCallUs_.store(0,std::memory_order_relaxed);
    readCallMaxUs_.store(0,std::memory_order_relaxed);
    videoGaps_.store(0,std::memory_order_relaxed);
    videoGapUs_.store(0,std::memory_order_relaxed);
    videoGapMaxUs_.store(0,std::memory_order_relaxed);
    packetQueueMax_.store(0,std::memory_order_relaxed);
    decodedFrames_.store(0,std::memory_order_relaxed);
    frameQueueMax_.store(0,std::memory_order_relaxed);
    fetchedFrames_.store(0,std::memory_order_relaxed);
    repaints_.store(0,std::memory_order_relaxed);
    paintGLs_.store(0,std::memory_order_relaxed);
    signalWaitUs_.store(0,std::memory_order_relaxed);
    signalWaitMaxUs_.store(0,std::memory_order_relaxed);
    paintLagUs_.store(0,std::memory_order_relaxed);
    paintLagMaxUs_.store(0,std::memory_order_relaxed);
}

void VideoPullStats::Reset()
{
    if(!kEnabled)
    {
        return;
    }
    started_.store(false,std::memory_order_relaxed);
    paintPendingUs_.store(0,std::memory_order_relaxed);
    lastVideoPacketUs_.store(0,std::memory_order_relaxed);
    ResetInterval();
}

void VideoPullStats::OnVideoPacketQueued(int queueLen)
{
    if(!kEnabled)
    {
        return;
    }
    //读包数和压缩包队列峰值都由 demux 线程在入队后记录，
    //入队后正是队列最长的时刻，采样即峰值
    readPackets_.fetch_add(1,std::memory_order_relaxed);
    UpdateMax(packetQueueMax_,queueLen);
}

void VideoPullStats::OnReadFrame(qint64 readUs, qint64 arrivalUs, bool videoPacket)
{
    if(!kEnabled)
    {
        return;
    }
    readCalls_.fetch_add(1,std::memory_order_relaxed);
    readCallUs_.fetch_add(readUs,std::memory_order_relaxed);
    UpdateMax(readCallMaxUs_,readUs);

    if(videoPacket)
    {
        const qint64 previousUs = lastVideoPacketUs_.exchange(arrivalUs,std::memory_order_relaxed);
        if(previousUs > 0 && arrivalUs > previousUs)
        {
            const qint64 gapUs = arrivalUs - previousUs;
            videoGaps_.fetch_add(1,std::memory_order_relaxed);
            videoGapUs_.fetch_add(gapUs,std::memory_order_relaxed);
            UpdateMax(videoGapMaxUs_,gapUs);
        }
    }
}

void VideoPullStats::OnFrameDecoded(int queueLen)
{
    if(!kEnabled)
    {
        return;
    }
    //解码帧队列峰值同样由生产者（解码线程）在入队后记录
    decodedFrames_.fetch_add(1,std::memory_order_relaxed);
    UpdateMax(frameQueueMax_,queueLen);
}

void VideoPullStats::OnFrameFetched()
{
    if(!kEnabled)
    {
        return;
    }
    //取帧后立即发信号，两者一一对应，记一个数
    fetchedFrames_.fetch_add(1,std::memory_order_relaxed);
}

qint64 VideoPullStats::OnRepaint(qint64 emitUs)
{
    if(!kEnabled)
    {
        return 0;
    }
    const qint64 nowUs = NowUs();
    if(emitUs > 0 && nowUs > emitUs)
    {
        const qint64 waitUs = nowUs - emitUs;
        signalWaitUs_.fetch_add(waitUs,std::memory_order_relaxed);
        UpdateMax(signalWaitMaxUs_,waitUs);
    }
    repaints_.fetch_add(1,std::memory_order_relaxed);
    return nowUs;
}

void VideoPullStats::OnPaintScheduled(qint64 repaintUs)
{
    if(!kEnabled)
    {
        return;
    }
    //同一时刻多次 update() 会被 Qt 合并成一次 paintGL，
    //这里只保留最后一次进入 Repaint 的时刻，滞后按它计算
    paintPendingUs_.store(repaintUs,std::memory_order_relaxed);
}

void VideoPullStats::OnPaintGL()
{
    if(!kEnabled)
    {
        return;
    }
    const qint64 pendingUs = paintPendingUs_.exchange(0,std::memory_order_relaxed);
    const qint64 nowUs = NowUs();
    if(pendingUs > 0 && nowUs > pendingUs)
    {
        const qint64 lagUs = nowUs - pendingUs;
        paintLagUs_.fetch_add(lagUs,std::memory_order_relaxed);
        UpdateMax(paintLagMaxUs_,lagUs);
    }
    paintGLs_.fetch_add(1,std::memory_order_relaxed);
}

void VideoPullStats::ReportIfDue(int packetQueueLen, int frameQueueLen)
{
    if(!kEnabled)
    {
        return;
    }

    const qint64 nowUs = NowUs();
    if(!started_.load(std::memory_order_relaxed))
    {
        //第一次调用只对齐窗口起点，不输出半个周期的不完整数据
        started_.store(true,std::memory_order_relaxed);
        intervalBeginUs_.store(nowUs,std::memory_order_relaxed);
        ResetInterval();
        return;
    }

    const qint64 elapsedUs = nowUs - intervalBeginUs_.load(std::memory_order_relaxed);
    if(elapsedUs < static_cast<qint64>(kReportIntervalUs))
    {
        return;
    }

    //均值只统计本窗口内有等待记录的重绘；队列当前长度是输出时刻的抽样
    const qint64 repaintCount = repaints_.load(std::memory_order_relaxed);
    const qint64 paintCount = paintGLs_.load(std::memory_order_relaxed);
    const qint64 readCount = readCalls_.load(std::memory_order_relaxed);
    const qint64 gapCount = videoGaps_.load(std::memory_order_relaxed);

    qInfo() << QString("[PULL-STATS] 读包率 = %1 解码率 = %2 取帧率 = %3 重绘率 = %4 绘制率 = %5 "
                       "包队列当前 = %6 包队列峰值 = %7 帧队列当前 = %8 帧队列峰值 = %9 "
                       "信号等待均值ms = %10 信号等待峰值ms = %11 绘制滞后均值ms = %12 绘制滞后峰值ms = %13 "
                       "读帧调用均值ms = %14 读帧调用峰值ms = %15 视频包间隔均值ms = %16 视频包间隔峰值ms = %17")
                   .arg(readPackets_.load(std::memory_order_relaxed) * 1000000.0 / elapsedUs,0,'f',1)
                   .arg(decodedFrames_.load(std::memory_order_relaxed) * 1000000.0 / elapsedUs,0,'f',1)
                   .arg(fetchedFrames_.load(std::memory_order_relaxed) * 1000000.0 / elapsedUs,0,'f',1)
                   .arg(repaintCount * 1000000.0 / elapsedUs,0,'f',1)
                   .arg(paintCount * 1000000.0 / elapsedUs,0,'f',1)
                   .arg(packetQueueLen)
                   .arg(packetQueueMax_.load(std::memory_order_relaxed))
                   .arg(frameQueueLen)
                   .arg(frameQueueMax_.load(std::memory_order_relaxed))
                   .arg(repaintCount > 0 ? signalWaitUs_.load(std::memory_order_relaxed) / 1000.0 / repaintCount : 0.0,0,'f',1)
                   .arg(signalWaitMaxUs_.load(std::memory_order_relaxed) / 1000.0,0,'f',1)
                   .arg(paintCount > 0 ? paintLagUs_.load(std::memory_order_relaxed) / 1000.0 / paintCount : 0.0,0,'f',1)
                                      .arg(paintLagMaxUs_.load(std::memory_order_relaxed) / 1000.0,0,'f',1)
                   .arg(readCount > 0 ? readCallUs_.load(std::memory_order_relaxed) / 1000.0 / readCount : 0.0,0,'f',1)
                   .arg(readCallMaxUs_.load(std::memory_order_relaxed) / 1000.0,0,'f',1)
                   .arg(gapCount > 0 ? videoGapUs_.load(std::memory_order_relaxed) / 1000.0 / gapCount : 0.0,0,'f',1)
                   .arg(videoGapMaxUs_.load(std::memory_order_relaxed) / 1000.0,0,'f',1);

    intervalBeginUs_.store(nowUs,std::memory_order_relaxed);
    ResetInterval();
}
