#include "GDISreenScapture.h"

#include <chrono>
#include <cstring>
#include <string>
#include <windows.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QDebug>

#include "AV_Common.h"

GDIScreenCapture::GDIScreenCapture()
    : stop_(false)
    , is_initialzed_(false)
    , width_(0)
    , height_(0)
    , capture_sequence_(0)
    , video_index_(-1)
    , framerate_(kTargetFramerate)
    , format_logged_(false)
    , mismatch_warned_(false)
    , input_format_(nullptr)
    , codec_context_(nullptr)
    , format_context_(nullptr)
    , av_frame_(nullptr)
    , av_packet_(nullptr)
    , frontIndex_(0)
    , middleIndex_(1)
    , backIndex_(2)
    , hasNewFrame_(false)
    , stopped_(false)
{
    avdevice_register_all();
    av_frame_ = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame* ptr) { av_frame_free(&ptr); });
    av_packet_ = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* ptr) { av_packet_free(&ptr); });
}

GDIScreenCapture::~GDIScreenCapture()
{
    Close();
}

quint32 GDIScreenCapture::GetWidth() const
{
    return width_;
}

quint32 GDIScreenCapture::GetHeight() const
{
    return height_;
}

quint64 GDIScreenCapture::GetCaptureSequence() const
{
    return capture_sequence_.load();
}

bool GDIScreenCapture::Init(qint64 display_index)
{
    if(is_initialzed_)
    {
        return true;
    }

    Q_UNUSED(display_index);

    const POINT primaryPoint{0, 0};
    const HMONITOR primaryMonitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if(!primaryMonitor || !GetMonitorInfo(primaryMonitor, &monitorInfo))
    {
        qWarning() << "failed to query the primary monitor capture rectangle";
        return false;
    }

    const RECT& primaryRect = monitorInfo.rcMonitor;
    const LONG captureWidth = primaryRect.right - primaryRect.left;
    const LONG captureHeight = primaryRect.bottom - primaryRect.top;
    if(captureWidth <= 0 || captureHeight <= 0)
    {
        qWarning() << "primary monitor has an invalid capture size" << captureWidth << "x" << captureHeight;
        return false;
    }

    const std::string captureSize = std::to_string(captureWidth) + "x" + std::to_string(captureHeight);
    AVDictionary* options = nullptr;
    av_dict_set_int(&options, "framerate", framerate_, AV_DICT_MATCH_CASE);
    av_dict_set_int(&options, "draw_mouse", 1, AV_DICT_MATCH_CASE);
    av_dict_set_int(&options, "offset_x", primaryRect.left, AV_DICT_MATCH_CASE);
    av_dict_set_int(&options, "offset_y", primaryRect.top, AV_DICT_MATCH_CASE);
    av_dict_set(&options, "video_size", captureSize.c_str(), AV_DICT_MATCH_CASE);
    qInfo() << "gdigrab primary monitor capture" << primaryRect.left << primaryRect.top
            << captureWidth << "x" << captureHeight;

    input_format_ = const_cast<AVInputFormat*>(av_find_input_format("gdigrab"));
    if(!input_format_)
    {
        qWarning() << "av_find_input_format failed";
        return false;
    }

    format_context_ = avformat_alloc_context();
    if(avformat_open_input(&format_context_, "desktop", input_format_, &options) != 0)
    {
        qWarning() << "avformat_open_input failed";
        return false;
    }

    if(avformat_find_stream_info(format_context_, nullptr) < 0)
    {
        qWarning() << "avformat_find_stream_info failed";
        Close();
        return false;
    }

    int videoIndex = -1;
    for(uint32_t i = 0; i < format_context_->nb_streams; ++i)
    {
        if(format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoIndex = static_cast<int>(i);
            break;
        }
    }
    if(videoIndex == -1)
    {
        qWarning() << "gdigrab returned no video stream";
        Close();
        return false;
    }

    width_ = format_context_->streams[videoIndex]->codecpar->width;
    height_ = format_context_->streams[videoIndex]->codecpar->height;
    if(width_ == 0 || height_ == 0)
    {
        qWarning() << "gdigrab returned an invalid capture size" << width_ << "x" << height_;
        Close();
        return false;
    }

    AVCodec* codec = const_cast<AVCodec*>(avcodec_find_decoder(format_context_->streams[videoIndex]->codecpar->codec_id));
    if(!codec)
    {
        qWarning() << "gdigrab decoder not found";
        Close();
        return false;
    }

    codec_context_ = avcodec_alloc_context3(codec);
    if(!codec_context_)
    {
        Close();
        return false;
    }

    avcodec_parameters_to_context(codec_context_, format_context_->streams[videoIndex]->codecpar);
    if(avcodec_open2(codec_context_, codec, nullptr) != 0)
    {
        Close();
        return false;
    }

    //三块缓冲在这里一次性分配，运行期不再分配。行跨度按像素布局取，
    //不依赖 AVFrame::linesize，也不使用 pkt_size。
    const qint32 rowBytes = av_image_get_linesize(kCapturePixelFormat, static_cast<qint32>(width_), 0);
    if(rowBytes <= 0)
    {
        qWarning() << "unsupported capture pixel format" << av_get_pix_fmt_name(kCapturePixelFormat);
        Close();
        return false;
    }
    for(CaptureFrameBuffer& buf : frameBuffers_)
    {
        buf.data.assign(static_cast<size_t>(rowBytes) * height_, 0);
        buf.width = width_;
        buf.height = height_;
        buf.stride = static_cast<quint32>(rowBytes);
        buf.validBytes = static_cast<quint32>(rowBytes) * height_;
    }
    frontIndex_ = 0;
    middleIndex_ = 1;
    backIndex_ = 2;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        hasNewFrame_ = false;
        stopped_ = false;//再次建立推流必须能重新握手
    }

    video_index_ = videoIndex;
    stop_ = false;
    is_initialzed_ = true;
    start();
    return true;
}

bool GDIScreenCapture::Close()
{
    //先唤醒消费者，避免它在采集线程停止后仍永久阻塞在条件变量上。
    //消费者必须在调用本函数之前 join，因为紧接着这里会释放三块缓冲区。
    RequestStop();
    if(is_initialzed_)
    {
        StopCapture();
    }
    if(codec_context_)
    {
        avcodec_free_context(&codec_context_);
    }
    if(format_context_)
    {
        avformat_close_input(&format_context_);
    }
    input_format_ = nullptr;
    video_index_ = -1;
    is_initialzed_ = false;
    stop_ = true;
    return true;
}

void GDIScreenCapture::RequestStop()
{
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        stopped_ = true;
    }
    frameReady_.notify_all();
}

bool GDIScreenCapture::WaitLatestFrame(CaptureFrameView& frame)
{
    if(frameBuffers_[0].data.empty())
    {
        return false;//Init 未完成或缓冲池已释放
    }

    std::unique_lock<std::mutex> lock(frameMutex_);
    frameReady_.wait(lock, [this] { return hasNewFrame_ || stopped_; });
    if(stopped_)
    {
        return false;//必须在交换之前判断，停止时不能改动索引
    }
    std::swap(frontIndex_, middleIndex_);
    hasNewFrame_ = false;
    lock.unlock();

    //front 只由本线程写入索引，采集线程不会碰它，锁外读取内容是安全的
    const CaptureFrameBuffer& front = frameBuffers_[frontIndex_];
    frame.data = front.data.data();
    frame.width = front.width;
    frame.height = front.height;
    frame.stride = front.stride;
    frame.size = front.validBytes;
    frame.sequence = front.sequence;
    frame.capturedAt = front.capturedAt;
    return true;
}

void GDIScreenCapture::run()
{
    //阶段二起采集线程完全由 av_read_frame() 驱动，编码线程由新帧通知驱动，两端都不再有固定休眠。
    while(is_initialzed_ && !stop_)
    {
        GetOneFrame();
    }
}

void GDIScreenCapture::StopCapture()
{
    if(is_initialzed_)
    {
        stop_ = true;
        if(isRunning())
        {
            quit();
            wait();
        }
    }
    std::lock_guard<std::mutex> lock(frameMutex_);
    for(CaptureFrameBuffer& buf : frameBuffers_)
    {
        buf.data.clear();
        buf.data.shrink_to_fit();
        buf.validBytes = 0;
    }
    hasNewFrame_ = false;
    frontIndex_ = 0;
    middleIndex_ = 1;
    backIndex_ = 2;
}

bool GDIScreenCapture::GetOneFrame()
{
    if(stop_)
    {
        return false;
    }

    const int ret = av_read_frame(format_context_, av_packet_.get());
    if(ret < 0)
    {
        return false;
    }

    if(av_packet_->stream_index == video_index_)
    {
        Decode(av_frame_.get(), av_packet_.get());
    }
    av_packet_unref(av_packet_.get());
    return true;
}

bool GDIScreenCapture::Decode(AVFrame* av_frame, AVPacket* av_packet)
{
    int ret = avcodec_send_packet(codec_context_, av_packet);
    if(ret < 0)
    {
        return false;
    }

    ret = avcodec_receive_frame(codec_context_, av_frame);
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return false;
    }
    if(ret < 0)
    {
        return false;
    }

    if(!format_logged_)
    {
        //文档 6.1 要求实测确认采集的实际输出格式：只改单侧常量正是红蓝颠倒的成因
        qInfo() << "gdigrab decoded frame format"
                << av_get_pix_fmt_name(static_cast<AVPixelFormat>(av_frame->format))
                << "linesize" << av_frame->linesize[0];
        format_logged_ = true;
    }

    if(av_frame->width != static_cast<int>(width_) || av_frame->height != static_cast<int>(height_))
    {
        if(!mismatch_warned_)
        {
            qWarning() << "decoded frame size changed" << av_frame->width << "x" << av_frame->height
                       << "expected" << width_ << "x" << height_;
            mismatch_warned_ = true;
        }
        av_frame_unref(av_frame);
        return false;
    }

    //back 只有采集线程写，整帧复制与打点都在锁外完成，锁内只交换索引
    CaptureFrameBuffer& writeBuffer = frameBuffers_[backIndex_];
    for(quint32 y = 0; y < writeBuffer.height; ++y)
    {
        //目标是池的紧凑跨度，源是解码帧自己的对齐跨度，两者不可互换
        std::memcpy(writeBuffer.data.data() + y * writeBuffer.stride,
                    av_frame->data[0] + y * av_frame->linesize[0],
                    writeBuffer.stride);
    }
    writeBuffer.sequence = ++capture_sequence_;
    writeBuffer.capturedAt = std::chrono::steady_clock::now();
    av_frame_unref(av_frame);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        std::swap(backIndex_, middleIndex_);
        hasNewFrame_ = true;
    }
    frameReady_.notify_one();//解锁后再通知
    return true;
}
