#include "CameraCapture.h"

#include <chrono>
#include <cstring>
#include <string>

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

CameraCapture::CameraCapture()
{
    av_frame_ = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame* ptr) { av_frame_free(&ptr); });
    av_packet_ = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* ptr) { av_packet_free(&ptr); });
}

CameraCapture::~CameraCapture()
{
    Close();
}

bool CameraCapture::Init()
{
    //再次建立推流必须能重新握手；已在运行则直接返回上次结果
    if(worker_.joinable())
    {
        return initOk_;
    }

    capturedFrames_.store(0);
    publishedFrames_.store(0);
    stop_.store(false);
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        hasNewFrame_ = false;
        stopped_ = false;
        initDone_ = false;
        initOk_ = false;
    }

    worker_ = std::thread([this] { Run(); });

    //等设备真正打开、宽高就绪后再返回，保证 GetWidth/GetHeight 立即可用
    std::unique_lock<std::mutex> lock(frameMutex_);
    const bool done = initReady_.wait_for(lock, std::chrono::seconds(5),
                                          [this] { return initDone_; });
    lock.unlock();
    if(!done || !initOk_)
    {
        qWarning() << "[CAM] device init failed or timed out";
        Close();
        return false;
    }
    return true;
}

bool CameraCapture::WaitLatestFrame(VideoFrame& frame)
{
    if(!ready_.load())
    {
        return false;//Init 未完成或设备已释放
    }

    std::unique_lock<std::mutex> lock(frameMutex_);
    frameReady_.wait(lock, [this] { return hasNewFrame_ || stopped_; });
    if(stopped_)
    {
        return false;//必须在交换之前判断，停止时不能改动索引
    }
    std::swap(frontIndex_, middleIndex_);
    hasNewFrame_ = false;

    //front 只由本线程写入索引，采集线程不会碰它，锁外读取内容是安全的。
    //owner 留空、data 指向内部三缓冲的 front，有效期到下一次 WaitLatestFrame。
    const CaptureFrameBuffer& front = frameBuffers_[frontIndex_];
    frame.kind = VideoFrameKind::Cpu;
    frame.gpu.reset();
    frame.cpu.owner.reset();
    frame.cpu.data = front.data.data();
    frame.cpu.stride = front.stride;
    frame.width = front.width;
    frame.height = front.height;
    frame.sequence = front.sequence;
    frame.capturedAt = front.capturedAt;
    lock.unlock();
    return true;
}

void CameraCapture::RequestStop()
{
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        stopped_ = true;
    }
    frameReady_.notify_all();
    initReady_.notify_all();
}

bool CameraCapture::Close()
{
    //先唤醒消费者，再回收工作线程；清空缓冲放在 join 之后，避免与仍在使用旧帧的消费者竞争
    RequestStop();
    if(worker_.joinable())
    {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(frameMutex_);
    for(CaptureFrameBuffer& buf : frameBuffers_)
    {
        buf.data.clear();
        buf.data.shrink_to_fit();
        buf.width = 0;
        buf.height = 0;
        buf.stride = 0;
    }
    hasNewFrame_ = false;
    frontIndex_ = 0;
    middleIndex_ = 1;
    backIndex_ = 2;
    width_.store(0);
    height_.store(0);
    ready_.store(false);
    initOk_ = false;
    return true;
}

int CameraCapture::InterruptCb(void* opaque)
{
    CameraCapture* self = static_cast<CameraCapture*>(opaque);
    return (self && self->stop_.load()) ? 1 : 0;
}

void CameraCapture::Run()
{
    if(!OpenDevice())
    {
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            initOk_ = false;
            initDone_ = true;
        }
        initReady_.notify_all();
        ReleaseDevice();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        initOk_ = true;
        initDone_ = true;
    }
    initReady_.notify_all();

    //读取完全由 av_read_frame 驱动：dshow 每交付一帧返回一次，停止时最迟在下一帧到达后退出
    while(!stop_.load() && GetOneFrame())
    {
    }

    ReleaseDevice();

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        stopped_ = true;
    }
    frameReady_.notify_all();
}

std::string CameraCapture::PickDefaultDevice()
{
    AVDeviceInfoList* deviceList = nullptr;
    const int listResult = avdevice_list_input_sources(input_format_, nullptr, nullptr, &deviceList);
    if(listResult < 0 || !deviceList)
    {
        qWarning() << "[CAM] avdevice_list_input_sources failed, err =" << listResult;
        if(deviceList)
        {
            avdevice_free_list_devices(&deviceList);
        }
        return std::string();
    }

    std::string picked;
    for(int i = 0; i < deviceList->nb_devices && picked.empty(); ++i)
    {
        AVDeviceInfo* device = deviceList->devices[i];
        if(!device)
        {
            continue;
        }
        for(int m = 0; m < device->nb_media_types; ++m)
        {
            if(device->media_types[m] == AVMEDIA_TYPE_VIDEO && device->device_name)
            {
                picked = device->device_name;
                break;
            }
        }
    }
    avdevice_free_list_devices(&deviceList);
    return picked;
}

bool CameraCapture::OpenDevice()
{
    avdevice_register_all();

    input_format_ = const_cast<AVInputFormat*>(av_find_input_format("dshow"));
    if(!input_format_)
    {
        qWarning() << "[CAM] dshow input format unavailable in this FFmpeg build";
        return false;
    }

    const std::string deviceName = PickDefaultDevice();
    if(deviceName.empty())
    {
        qWarning() << "[CAM] no camera video device found";
        return false;
    }
    device_name_ = deviceName;

    format_context_ = avformat_alloc_context();
    if(!format_context_)
    {
        qWarning() << "[CAM] avformat_alloc_context failed";
        return false;
    }
    format_context_->interrupt_callback.callback = &CameraCapture::InterruptCb;
    format_context_->interrupt_callback.opaque = this;
    //设备就绪后才交付帧，限制探测时长避免打开阶段长时间阻塞
    format_context_->max_analyze_duration = 2 * AV_TIME_BASE;

    //dshow 从 URL 解析设备名（video=<名称>），不是 AVOption；不设 video_size/framerate，使用设备默认模式
    const std::string url = "video=" + deviceName;
    const int openResult = avformat_open_input(&format_context_, url.c_str(), input_format_, nullptr);
    if(openResult < 0)
    {
        qWarning() << "[CAM] avformat_open_input failed for" << QString::fromStdString(deviceName)
                   << "err =" << openResult << "(device busy or access denied?)";
        return false;
    }

    if(avformat_find_stream_info(format_context_, nullptr) < 0)
    {
        qWarning() << "[CAM] avformat_find_stream_info failed";
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
        qWarning() << "[CAM] dshow returned no video stream";
        return false;
    }

    AVStream* stream = format_context_->streams[videoIndex];
    const quint32 width = stream->codecpar->width;
    const quint32 height = stream->codecpar->height;
    if(width == 0 || height == 0)
    {
        qWarning() << "[CAM] dshow returned an invalid capture size" << width << "x" << height;
        return false;
    }
    width_.store(width);
    height_.store(height);

    //声明帧率仅作设备信息，不当作实测速率；有效时取 avg_frame_rate，否则退回 r_frame_rate
    const AVRational rate = (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
                            ? stream->avg_frame_rate : stream->r_frame_rate;
    declared_framerate_ = rate.den > 0
                          ? static_cast<qint32>(rate.num / rate.den) : 0;

    AVCodec* codec = const_cast<AVCodec*>(avcodec_find_decoder(stream->codecpar->codec_id));
    if(!codec)
    {
        qWarning() << "[CAM] decoder not found";
        return false;
    }
    codec_context_ = avcodec_alloc_context3(codec);
    if(!codec_context_)
    {
        return false;
    }
    avcodec_parameters_to_context(codec_context_, stream->codecpar);
    if(avcodec_open2(codec_context_, codec, nullptr) != 0)
    {
        qWarning() << "[CAM] decoder open failed";
        return false;
    }

    //三块缓冲在这里一次性分配，运行期不再分配；输出统一 BGRA，行跨度按像素布局取
    const qint32 rowBytes = av_image_get_linesize(kCapturePixelFormat, static_cast<qint32>(width), 0);
    if(rowBytes <= 0)
    {
        qWarning() << "[CAM] unsupported output pixel format" << av_get_pix_fmt_name(kCapturePixelFormat);
        return false;
    }
    for(CaptureFrameBuffer& buf : frameBuffers_)
    {
        buf.data.assign(static_cast<size_t>(rowBytes) * height, 0);
        buf.width = width;
        buf.height = height;
        buf.stride = static_cast<quint32>(rowBytes);
    }
    frontIndex_ = 0;
    middleIndex_ = 1;
    backIndex_ = 2;

    //每次会话重置时间轴与帧计数：序号重新以本会话起点为零点量化
    lastSequence_ = 0;
    sessionStart_ = std::chrono::steady_clock::now();
    video_index_ = videoIndex;
    ready_.store(true);

    qInfo() << "[CAM] device opened" << QString::fromStdString(deviceName)
            << "size =" << width << "x" << height
            << "input format =" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(stream->codecpar->format))
            << "declared framerate =" << declared_framerate_;
    return true;
}

void CameraCapture::ReleaseDevice()
{
    ready_.store(false);
    if(sws_context_)
    {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
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
}

bool CameraCapture::GetOneFrame()
{
    if(stop_.load())
    {
        return false;
    }

    const int ret = av_read_frame(format_context_, av_packet_.get());
    if(ret < 0)
    {
        //停止时的中断属正常退出；其它错误说明设备已不可用
        if(!stop_.load() && ret != AVERROR_EXIT)
        {
            qWarning() << "[CAM] av_read_frame failed, err =" << ret << ", stopping session";
        }
        return false;
    }

    if(av_packet_->stream_index == video_index_)
    {
        Decode(av_frame_.get(), av_packet_.get());
    }
    av_packet_unref(av_packet_.get());
    return true;
}

bool CameraCapture::Decode(AVFrame* av_frame, AVPacket* av_packet)
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

    //第一版不允许运行中改尺寸：报错并停止当前会话，重开时按新尺寸建立编码器
    if(av_frame->width != static_cast<int>(width_.load()) ||
       av_frame->height != static_cast<int>(height_.load()))
    {
        if(!mismatch_warned_)
        {
            qWarning() << "[CAM] decoded frame size changed" << av_frame->width << "x" << av_frame->height
                       << "expected" << width_.load() << "x" << height_.load() << ", stopping session";
            mismatch_warned_ = true;
        }
        stop_.store(true);
        av_frame_unref(av_frame);
        return false;
    }

    const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(av_frame->format);
    if(!format_logged_)
    {
        //实测确认设备的实际输出格式，NV12/YUY2/MJPEG 解码结果都要统一转 BGRA
        qInfo() << "[CAM] decoded frame format" << av_get_pix_fmt_name(srcFormat)
                << "linesize" << av_frame->linesize[0];
        format_logged_ = true;
    }

    //back 只有采集线程写，转换在锁外完成，锁内只交换索引
    CaptureFrameBuffer& writeBuffer = frameBuffers_[backIndex_];
    //自己缓存转换上下文：yuvj* 这类废弃格式在 sws 内部会被改写为对应的非 J 格式（如 yuvj420p→yuv420p），
    //sws_getCachedContext 的参数比较因此永不相等，会每帧重建并打印 deprecation 警告；
    //这里按原始源参数自行判断，只在真正变化时重建。
    if(!sws_context_ ||
       sws_src_format_ != av_frame->format ||
       sws_src_width_ != av_frame->width ||
       sws_src_height_ != av_frame->height)
    {
        if(sws_context_)
        {
            sws_freeContext(sws_context_);
            sws_context_ = nullptr;
        }
        sws_context_ = sws_getContext(av_frame->width, av_frame->height, srcFormat,
                                      static_cast<int>(writeBuffer.width), static_cast<int>(writeBuffer.height),
                                      kCapturePixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if(!sws_context_)
        {
            qWarning() << "[CAM] failed to create swscale context for" << av_get_pix_fmt_name(srcFormat);
            av_frame_unref(av_frame);
            return false;
        }
        sws_src_format_ = av_frame->format;
        sws_src_width_ = av_frame->width;
        sws_src_height_ = av_frame->height;
    }

    uint8_t* dstData[4] = { writeBuffer.data.data(), nullptr, nullptr, nullptr };
    const int dstStride[4] = { static_cast<int>(writeBuffer.stride), 0, 0, 0 };
    sws_scale(sws_context_, av_frame->data, av_frame->linesize, 0, av_frame->height, dstData, dstStride);

    //序号由真实单调时钟量化而来；两帧过近被量化到同一格时强制递增，保证严格单调
    const std::chrono::steady_clock::time_point capturedAt = std::chrono::steady_clock::now();
    quint64 sequence = MakeSequence(capturedAt, sessionStart_);
    if(sequence <= lastSequence_)
    {
        sequence = lastSequence_ + 1;
    }
    lastSequence_ = sequence;
    writeBuffer.sequence = sequence;
    writeBuffer.capturedAt = capturedAt;
    ++capturedFrames_;
    ++publishedFrames_;
    av_frame_unref(av_frame);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        std::swap(backIndex_, middleIndex_);
        hasNewFrame_ = true;
    }
    frameReady_.notify_one();//解锁后再通知
    return true;
}
