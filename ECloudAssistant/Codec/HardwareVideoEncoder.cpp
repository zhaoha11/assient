#include "HardwareVideoEncoder.h"
#include "D3D11SharedContext.h"
#include "VideoFrame.h"

#include <chrono>
#include <mutex>
#include <d3d11.h>
#include <wrl/client.h>

extern "C"
{
    #include <libavutil/opt.h>
    #include <libavutil/hwcontext.h>
    #include <libavutil/hwcontext_d3d11va.h>
}

using Microsoft::WRL::ComPtr;

// GPU 模式的全部原生资源：VideoProcessor 与它对应的输入尺寸。
// 输入尺寸变化（采集分辨率改变）时整体重建。
struct HardwareVideoEncoder::GpuEncodeResources
{
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    unsigned int inputWidth = 0;
    unsigned int inputHeight = 0;

    // 按输入纹理的实际尺寸懒建 VideoProcessor；尺寸未变则复用。
    bool ensure(D3D11SharedContext& shared,AVCodecContext* codecContext,
                ID3D11Texture2D* inputTexture);
};

bool HardwareVideoEncoder::GpuEncodeResources::ensure(D3D11SharedContext& shared,
                                                      AVCodecContext* codecContext,
                                                      ID3D11Texture2D* inputTexture)
{
    D3D11_TEXTURE2D_DESC inputDesc{};
    inputTexture->GetDesc(&inputDesc);
    if(processor && inputDesc.Width == inputWidth && inputDesc.Height == inputHeight)
    {
        return true;
    }

    processor.Reset();
    enumerator.Reset();
    inputWidth = inputDesc.Width;
    inputHeight = inputDesc.Height;

    ID3D11VideoDevice* videoDevice = shared.videoDevice();
    ID3D11VideoContext* videoContext = shared.videoContext();
    if(!videoDevice || !videoContext) return false;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = inputWidth;
    contentDesc.InputHeight = inputHeight;
    contentDesc.OutputWidth = static_cast<UINT>(codecContext->width);
    contentDesc.OutputHeight = static_cast<UINT>(codecContext->height);
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HRESULT hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc,enumerator.GetAddressOf());
    if(FAILED(hr))
    {
        qWarning() << "[ENCODE] CreateVideoProcessorEnumerator failed" << Qt::hex << hr
                   << "input" << inputWidth << "x" << inputHeight;
        return false;
    }
    hr = videoDevice->CreateVideoProcessor(enumerator.Get(),0,processor.GetAddressOf());
    if(FAILED(hr))
    {
        qWarning() << "[ENCODE] CreateVideoProcessor failed" << Qt::hex << hr;
        return false;
    }

    const RECT srcRect{0,0,static_cast<LONG>(inputWidth),static_cast<LONG>(inputHeight)};
    const RECT dstRect{0,0,static_cast<LONG>(codecContext->width),
                       static_cast<LONG>(codecContext->height)};
    videoContext->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,&srcRect);
    videoContext->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,&dstRect);
    videoContext->VideoProcessorSetOutputTargetRect(processor.Get(),TRUE,&dstRect);

    // 桌面是 0-255 全范围 RGB，输出 NV12 按 BT.709；漏设会让拉流端偏灰/偏色
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColorSpace{};
    inputColorSpace.RGB_Range = 0;//0-255
    inputColorSpace.YCbCr_Matrix = 1;//BT.709
    inputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&inputColorSpace);

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColorSpace{};
    outputColorSpace.YCbCr_Matrix = 1;//BT.709
    outputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&outputColorSpace);

    qInfo() << "[ENCODE] video processor ready," << inputWidth << "x" << inputHeight
            << "BGRA ->" << codecContext->width << "x" << codecContext->height << "NV12";
    return true;
}

HardwareVideoEncoder::HardwareVideoEncoder(const char* codecName, D3D11SharedContext* shared)
    :codecName_(codecName ? codecName : "")
    ,shared_(shared)
{
    // 只有 nvenc + 外部共享设备才走 GPU 直通；其它硬编保持 CPU 输入，避免误开半成品路径
    gpuMode_ = (shared_ != nullptr && codecName_ == "h264_nvenc");
    if(gpuMode_)
    {
        gpu_.reset(new GpuEncodeResources());
        gpuPacket_.reset(av_packet_alloc(),[](AVPacket* ptr){ av_packet_free(&ptr); });
    }
}

HardwareVideoEncoder::~HardwareVideoEncoder()
{
    Close();
}

AVCodec* HardwareVideoEncoder::FindCodec()
{
    if(codecName_.empty())
    {
        return nullptr;
    }
    //硬件编码器按名字查找：h264_nvenc / h264_qsv / h264_amf
    return const_cast<AVCodec*>(avcodec_find_encoder_by_name(codecName_.c_str()));
}

bool HardwareVideoEncoder::ConfigureCodec()
{
    if(gpuMode_)
    {
        // GPU 模式：输入是 D3D11 纹理，输出 NV12 硬件帧，全程不经过 CPU 转换器
        codecContext_->pix_fmt = AV_PIX_FMT_D3D11;
        if(!ConfigureGpuFrames())
        {
            return false;
        }
        SetEncoderOptions();
        return true;
    }

    // CPU 模式：硬编普遍只吃 NV12，由基类转换器把采集侧 BGRA 转成 NV12；
    // 目标格式取 codecContext_->pix_fmt，所以这里设成 NV12 即可，不必改转换器调用方
    codecContext_->pix_fmt = AV_PIX_FMT_NV12;
    //不设 profile/level：各厂商默认档位不同，照搬 x264 的 BASELINE/40 可能被拒
    SetEncoderOptions();
    return true;
}

// 把采集侧共享的 D3D11 设备注入 FFmpeg，并建 NV12 硬件帧池供编码器消费。
bool HardwareVideoEncoder::ConfigureGpuFrames()
{
    if(!shared_ || !shared_->device() || !shared_->videoDevice() || !shared_->videoContext())
    {
        qWarning() << "[ENCODE] GPU passthrough unavailable: shared device/video interface missing";
        return false;
    }

    // 1) 注入外部 device：绝不能调 av_hwdevice_ctx_create，那会另建一个 device 与采集侧不共享。
    //    hw device ctx 的 uninit 会无条件 Release，所以这里必须先 AddRef 转移一份所有权。
    AVBufferRef* deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if(!deviceRef) return false;
    // deviceRef->data 是 AVHWDeviceContext，d3d11va 专属结构挂在它的 hwctx 上。
    // 直接按 AVD3D11VADeviceContext 解释 data 会覆盖 AVHWDeviceContext 的 av_class/internal，
    // FFmpeg 随后任一次日志或初始化即崩溃。
    auto* deviceCtx = reinterpret_cast<AVHWDeviceContext*>(deviceRef->data);
    auto* deviceHwCtx = reinterpret_cast<AVD3D11VADeviceContext*>(deviceCtx->hwctx);
    deviceHwCtx->device = shared_->device();
    deviceHwCtx->device->AddRef();
    if(av_hwdevice_ctx_init(deviceRef) < 0)
    {
        av_buffer_unref(&deviceRef);
        qWarning() << "[ENCODE] av_hwdevice_ctx_init failed";
        return false;
    }

    // 2) NV12 硬件帧池。BindFlags 必须是 RENDER_TARGET：VideoProcessor 的输出视图要求，
    //    留 0 会被 FFmpeg 默认成 BIND_DECODER，导致创建纹理失败。
    AVBufferRef* framesRef = av_hwframe_ctx_alloc(deviceRef);
    if(!framesRef)
    {
        av_buffer_unref(&deviceRef);
        return false;
    }
    auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
    framesCtx->format = AV_PIX_FMT_D3D11;
    framesCtx->sw_format = AV_PIX_FMT_NV12;
    framesCtx->width = codecContext_->width;
    framesCtx->height = codecContext_->height;
    framesCtx->initial_pool_size = 4;
    auto* d3dFramesHwCtx = reinterpret_cast<AVD3D11VAFramesContext*>(framesCtx->hwctx);
    d3dFramesHwCtx->BindFlags = D3D11_BIND_RENDER_TARGET;
    if(av_hwframe_ctx_init(framesRef) < 0)
    {
        av_buffer_unref(&framesRef);
        av_buffer_unref(&deviceRef);
        qWarning() << "[ENCODE] av_hwframe_ctx_init failed";
        return false;
    }

    // 3) hw_frames_ctx 必须在 avcodec_open2 之前挂到编码器上下文上
    hwDeviceRef_ = deviceRef;
    hwFramesRef_ = framesRef;
    codecContext_->pix_fmt = AV_PIX_FMT_D3D11;
    codecContext_->hw_frames_ctx = av_buffer_ref(framesRef);
    qInfo() << "[ENCODE] hw frames ready," << codecContext_->width << "x" << codecContext_->height
            << "NV12";
    return true;
}

void HardwareVideoEncoder::SetEncoderOptions()
{
    if(codecName_ == "h264_nvenc")
    {
        av_opt_set(codecContext_->priv_data,"preset","p1",0);//最快档
        av_opt_set(codecContext_->priv_data,"tune","ull",0);//超低延迟
        // [LAT-D] 实测：默认输出延迟 D=2，nvenc 把 2 帧压在内部输出流水线里（≈66ms 端到端）。
        // zerolatency+delay=0 把 D 压到 0；代价是编码由异步转同步、单帧耗时涨到 ~8ms，
        // 但净延迟少 ~58ms。别因为「编码均值us 变大」误判成回退——那是延迟换了记法。
        av_opt_set(codecContext_->priv_data,"zerolatency","1",0);
        av_opt_set(codecContext_->priv_data,"delay","0",0);
    }
    else if(codecName_ == "h264_qsv")
    {
        av_opt_set(codecContext_->priv_data,"preset","veryfast",0);
        av_opt_set(codecContext_->priv_data,"async_depth","1",0);//降低内部排队延迟
    }
    else if(codecName_ == "h264_amf")
    {
        av_opt_set(codecContext_->priv_data,"usage","ultralowlatency",0);
        av_opt_set(codecContext_->priv_data,"quality","speed",0);
    }
}

void HardwareVideoEncoder::Close()
{
    gpu_.reset();
    gpuPacket_.reset();
    fatalError_.store(false);
    VideoEncoder::Close();
}

// GPU 直通：采集纹理 --(VideoProcessor)--> NV12 硬件帧 --(nvenc)--> 码流
AVPacketPtr HardwareVideoEncoder::EncodeGpuFrame(IGpuVideoFrame& gpu,qint64 pts,
                                                 VideoEncodeTiming* timing)
{
    if(!gpuMode_ || !is_initialzed_ || !gpu_ || !hwFramesRef_ || !codecContext_)
    {
        return nullptr;
    }
    // 采集侧（WGC）持有的纹理；类型在采集与硬编两处解释，公共头只见 void*
    auto* inputTexture = static_cast<ID3D11Texture2D*>(gpu.nativeTexture());
    if(!inputTexture) return nullptr;

    // 从硬件帧池取一块 NV12 纹理作为编码器输入
    AVFramePtr frame(av_frame_alloc(),[](AVFrame* ptr){ av_frame_free(&ptr); });
    if(!frame || av_hwframe_get_buffer(hwFramesRef_,frame.get(),0) < 0)
    {
        qWarning() << "[ENCODE] av_hwframe_get_buffer failed";
        fatalError_.store(true);
        return nullptr;
    }
    frame->pts = pts;
    auto* outputTexture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
    // FFmpeg 的 d3d11va 帧池是「一张数组纹理 + data[1] 存切片号」，
    // 输出视图必须按数组切片来取；若某版本改回单张 2D 纹理则退化为普通 2D 视图。
    const UINT outputSlice = static_cast<UINT>(reinterpret_cast<intptr_t>(frame->data[1]));
    D3D11_TEXTURE2D_DESC outputTextureDesc{};
    outputTexture->GetDesc(&outputTextureDesc);

    ID3D11VideoDevice* videoDevice = shared_->videoDevice();

    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    // 立即上下文非线程安全：采集线程也在用它做 CopyResource，建 view 与 Blt 要跟它串行。
    // 但锁只罩这一段 GPU 转换——send/receive 走 nvenc 自己的队列，不碰立即上下文；
    // delay=0 之后 receive 每帧阻塞数 ms，锁若带进去会让采集线程整帧排在编码后面。
    {
        std::lock_guard<std::mutex> lock(shared_->ContextLock());

        if(!gpu_->ensure(*shared_,codecContext_,inputTexture))
        {
            fatalError_.store(true);
            return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputDesc.Texture2D.MipSlice = 0;
        inputDesc.Texture2D.ArraySlice = 0;
        ComPtr<ID3D11VideoProcessorInputView> inputView;
        HRESULT hr = videoDevice->CreateVideoProcessorInputView(
                         inputTexture,gpu_->enumerator.Get(),&inputDesc,inputView.GetAddressOf());
        if(FAILED(hr))
        {
            qWarning() << "[ENCODE] CreateVideoProcessorInputView failed" << Qt::hex << hr;
            fatalError_.store(true);
            return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        if(outputTextureDesc.ArraySize > 1)
        {
            outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
            outputDesc.Texture2DArray.MipSlice = 0;
            outputDesc.Texture2DArray.FirstArraySlice = outputSlice;
            outputDesc.Texture2DArray.ArraySize = 1;
        }
        else
        {
            outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            outputDesc.Texture2D.MipSlice = 0;
        }
        ComPtr<ID3D11VideoProcessorOutputView> outputView;
        hr = videoDevice->CreateVideoProcessorOutputView(
                 outputTexture,gpu_->enumerator.Get(),&outputDesc,outputView.GetAddressOf());
        if(FAILED(hr))
        {
            qWarning() << "[ENCODE] CreateVideoProcessorOutputView failed" << Qt::hex << hr;
            fatalError_.store(true);
            return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.Get();

        hr = shared_->videoContext()->VideoProcessorBlt(gpu_->processor.Get(),outputView.Get(),0,1,&stream);
        if(FAILED(hr))
        {
            qWarning() << "[ENCODE] VideoProcessorBlt failed" << Qt::hex << hr;
            fatalError_.store(true);
            return nullptr;
        }
    }

    NoteFrameSent();
    const int sendResult = avcodec_send_frame(codecContext_,frame.get());
    if(sendResult < 0)
    {
        qWarning() << "[ENCODE] avcodec_send_frame failed" << sendResult;
        fatalError_.store(true);
        return nullptr;
    }
    const int ret = avcodec_receive_packet(codecContext_,gpuPacket_.get());
    if(timing)
    {
        timing->encodeUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - begin).count();
    }
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return nullptr;
    }
    if(ret < 0)
    {
        qWarning() << "[ENCODE] avcodec_receive_packet failed" << ret;
        fatalError_.store(true);
        return nullptr;
    }
    NotePacketReceived(pts,gpuPacket_.get());
    return gpuPacket_;
}
