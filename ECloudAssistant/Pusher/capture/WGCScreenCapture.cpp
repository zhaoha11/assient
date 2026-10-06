#include "WGCScreenCapture.h"
#include "AV_Common.h"
#include "D3D11SharedContext.h"

#include <QDebug>
#include <algorithm>
#include <cstring>
#include <memory>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <roapi.h>
#include <wrl/client.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace
{
enum class ReadResult { none, frame, error };

// GPU 帧句柄：持有自有纹理和 device，保证编码线程使用期间纹理有效；
// 同时持有 device 引用，即使采集端先退出也不会让纹理落在已销毁的 device 上。
// 这里（连同 WgcState）是采集侧唯一解释 ID3D11 类型的地方，公共头只见 void*。
class WgcGpuFrame : public IGpuVideoFrame
{
public:
    WgcGpuFrame(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11Device> device,
                quint32 width, quint32 height)
        : texture_(std::move(texture))
        , device_(std::move(device))
        , width_(width)
        , height_(height)
    {
    }

    quint32 width() const override { return width_; }
    quint32 height() const override { return height_; }
    VideoPixelFormat format() const override { return VideoPixelFormat::Bgra8; }
    void* nativeTexture() const override { return texture_.Get(); }
    void* nativeDevice() const override { return device_.Get(); }

private:
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11Device> device_;
    quint32 width_;
    quint32 height_;
};

struct WgcState
{
    bool gpuTexture = false;   // true：直接输出 GPU 纹理；false：读回 CPU BGRA
    // 上层注入的共享 device（可空）。非空时直接复用它，采集纹理才能与硬件编码器同 device。
    D3D11SharedContext* shared = nullptr;
    HMONITOR monitor = nullptr;
    winrt::Windows::Graphics::SizeInt32 poolSize{0,0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    wgd::IDirect3DDevice graphicsDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    ComPtr<ID3D11Texture2D> staging;
    UINT stagingWidth = 0;
    UINT stagingHeight = 0;

    // 先关闭捕获会话和帧池，再由工作线程解除 WinRT 初始化。
    ~WgcState()
    {
        try { if(session) session.Close(); } catch(const winrt::hresult_error&) {}
        try { if(pool) pool.Close(); } catch(const winrt::hresult_error&) {}
    }

    // 使用外部共享 device 时，立即上下文还要被编码线程做格式转换，必须互斥；
    // 自建 device 只有本线程在用，返回空锁即可。
    std::unique_lock<std::mutex> lockContext()
    {
        if(shared) return std::unique_lock<std::mutex>(shared->ContextLock());
        return std::unique_lock<std::mutex>();
    }

    // 为主显示器建立 D3D11 设备、WGC 帧池和采集会话；失败时交给上层回退或重试。
    bool Open()
    {
        try
        {
            // 工作线程可能停止后重启；每次都重新获取工厂，避免跨 RoUninitialize 复用。
            auto sessionFactory = winrt::get_activation_factory<wgc::IGraphicsCaptureSessionStatics>(
                winrt::name_of<wgc::GraphicsCaptureSession>());
            if(!sessionFactory.IsSupported())
            {
                qWarning() << "WGC is not supported on this desktop";
                return false;
            }

            const POINT primaryPoint{0,0};
            monitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);
            if(!monitor) return false;

            HRESULT hr = S_OK;
            if(shared && shared->device())
            {
                // 复用注入的共享 device / 立即上下文，与硬件编码器落在同一 device 上
                device = shared->device();
                device->GetImmediateContext(&context);
            }
            else
            {
                const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
                hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,
                                       nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
            }
            if(FAILED(hr))
            {
                qWarning() << "WGC D3D11CreateDevice failed" << Qt::hex << hr;
                return false;
            }
            ComPtr<IDXGIDevice> dxgiDevice;
            hr = device.As(&dxgiDevice);
            if(SUCCEEDED(hr))
                hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                      reinterpret_cast<IInspectable**>(winrt::put_abi(graphicsDevice)));
            if(FAILED(hr))
            {
                qWarning() << "WGC D3D11 interop failed" << Qt::hex << hr;
                return false;
            }

            auto itemInterop = winrt::get_activation_factory<IGraphicsCaptureItemInterop>(
                winrt::name_of<wgc::GraphicsCaptureItem>());
            hr = itemInterop->CreateForMonitor(monitor,winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                winrt::put_abi(item));
            if(FAILED(hr))
            {
                qWarning() << "WGC primary monitor item failed" << Qt::hex << hr;
                return false;
            }
            poolSize = item.Size();
            if(poolSize.Width <= 0 || poolSize.Height <= 0) return false;

            auto poolFactory = winrt::get_activation_factory<wgc::IDirect3D11CaptureFramePoolStatics2>(
                winrt::name_of<wgc::Direct3D11CaptureFramePool>());
            pool = poolFactory.CreateFreeThreaded(
                graphicsDevice,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,poolSize);
            session = pool.CreateCaptureSession(item);
            session.IsCursorCaptureEnabled(true);
            session.StartCapture();
            qInfo() << "WGC primary monitor capture" << poolSize.Width << "x" << poolSize.Height
                    << (gpuTexture ? "gpu texture" : "cpu readback");
            return true;
        }
        catch(const winrt::hresult_error& error)
        {
            qWarning() << "WGC setup failed" << Qt::hex << error.code().value;
            return false;
        }
    }

    // 内容尺寸变化时在释放当前帧后重建帧池，并同步输入尺寸。
    void MaybeRecreatePool(const winrt::Windows::Graphics::SizeInt32& content,
                           quint32 width, quint32 height)
    {
        if(content.Width == poolSize.Width && content.Height == poolSize.Height) return;
        poolSize = content;
        pool.Recreate(graphicsDevice,
                      winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                      2,poolSize);
        qInfo() << "WGC capture resized" << width << "x" << height;
    }

    // 一次最多取四帧，使用本次取到的最后一帧。
    // gpuTexture 为真时把纹理复制进自有的默认用法纹理，产出 GPU 帧；
    // 否则经 staging 读回紧凑 BGRA，产出 CPU 帧。
    // 无帧、取得新帧、读取失败分别返回 none、frame、error。
    ReadResult Read(VideoFrame& out)
    {
        wgc::Direct3D11CaptureFrame newest{nullptr};
        try
        {
            // 一次最多取四帧，丢弃本次取到的较旧帧。
            for(int i = 0; i < 4; ++i)
            {
                auto frame = pool.TryGetNextFrame();
                if(!frame) break;
                if(newest) newest.Close();
                newest = std::move(frame);
            }
            if(!newest) return ReadResult::none;

            const auto content = newest.ContentSize();
            auto surface = newest.Surface();
            auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void**>(texture.GetAddressOf()));
            if(FAILED(hr) || content.Width <= 0 || content.Height <= 0)
            {
                newest.Close();
                return ReadResult::error;
            }

            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if(desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
               static_cast<UINT>(content.Width) > desc.Width ||
               static_cast<UINT>(content.Height) > desc.Height)
            {
                newest.Close();
                return ReadResult::error;
            }
            const quint32 width = static_cast<quint32>(content.Width);
            const quint32 height = static_cast<quint32>(content.Height);

            if(gpuTexture)
            {
                // 不直接持有 pool 的纹理：pool 只有 2 块缓冲，被编码线程占住会饿死 TryGetNextFrame。
                // 改为复制进每帧新建的自有纹理，与 pool 回收彻底解耦（纹理池化留待后续优化）。
                D3D11_TEXTURE2D_DESC copyDesc = desc;
                copyDesc.Usage = D3D11_USAGE_DEFAULT;
                copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                copyDesc.CPUAccessFlags = 0;
                copyDesc.MiscFlags = 0;
                ComPtr<ID3D11Texture2D> owned;
                hr = device->CreateTexture2D(&copyDesc,nullptr,owned.GetAddressOf());
                if(FAILED(hr))
                {
                    newest.Close();
                    return ReadResult::error;
                }
                {
                    std::unique_lock<std::mutex> contextLock = lockContext();
                    context->CopyResource(owned.Get(),texture.Get());
                }

                out = VideoFrame{};
                out.kind = VideoFrameKind::Gpu;
                out.gpu = std::make_shared<WgcGpuFrame>(owned,device,width,height);
                out.width = width;
                out.height = height;

                texture.Reset();
                access = nullptr;
                surface = nullptr;
                newest.Close();
                newest = nullptr;

                MaybeRecreatePool(content,width,height);
                return ReadResult::frame;
            }

            // CPU 模式：复制到 staging 并映射，逐行去掉 GPU 行填充得到紧凑 BGRA。
            if(!staging || stagingWidth != desc.Width || stagingHeight != desc.Height)
            {
                staging.Reset();
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                hr = device->CreateTexture2D(&desc,nullptr,staging.GetAddressOf());
                if(FAILED(hr))
                {
                    newest.Close();
                    return ReadResult::error;
                }
                stagingWidth = desc.Width;
                stagingHeight = desc.Height;
            }

            context->CopyResource(staging.Get(),texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
            if(FAILED(hr))
            {
                newest.Close();
                return ReadResult::error;
            }
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            if(mapped.RowPitch < rowBytes)
            {
                context->Unmap(staging.Get(),0);
                newest.Close();
                return ReadResult::error;
            }
            auto newPixels = std::make_shared<std::vector<quint8>>(rowBytes * height);
            for(quint32 y = 0; y < height; ++y)
            {
                std::memcpy(newPixels->data() + y * rowBytes,
                            static_cast<const quint8*>(mapped.pData) + y * mapped.RowPitch,rowBytes);
            }
            context->Unmap(staging.Get(),0);

            out = VideoFrame{};
            out.kind = VideoFrameKind::Cpu;
            out.cpu.owner = std::move(newPixels);
            out.cpu.stride = static_cast<quint32>(rowBytes);
            out.width = width;
            out.height = height;

            texture.Reset();
            access = nullptr;
            surface = nullptr;
            newest.Close();
            newest = nullptr;

            MaybeRecreatePool(content,width,height);
            return ReadResult::frame;
        }
        catch(const winrt::hresult_error& error)
        {
            if(newest) newest.Close();
            qWarning() << "WGC frame read failed" << Qt::hex << error.code().value;
            return ReadResult::error;
        }
    }
};
} // namespace

// 析构时停止并等待工作线程，防止它继续访问本对象。
WGCScreenCapture::~WGCScreenCapture()
{
    Close();
}

// 选择输出形态。仅记录，在下一次采集会话建立时生效，不中途切换正在推的流。
bool WGCScreenCapture::SetOutput(CaptureOutput output)
{
    captureOutput_.store(output);
    return true;
}

// 注入与硬件编码共用的 device。仅记录，在下一次采集会话建立时生效。
bool WGCScreenCapture::SetExternalDevice(D3D11SharedContext* device)
{
    externalDevice_.store(device);
    return true;
}

// 启动采集线程，等待 WGC 初始化及三秒内的首帧；没有首帧就关闭并报告失败。
bool WGCScreenCapture::Init(qint64 display_index)
{
    Q_UNUSED(display_index); // 与 GDI 一样，当前只采集主显示器。
    if(worker_.joinable()) return initOk_;
    stop_.store(false);
    captureSequence_.store(0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = false;
        hasNewFrame_ = false;
        initDone_ = false;
        initOk_ = false;
        latestFrame_ = VideoFrame{};
    }
    worker_ = std::thread([this]{ Run(); });
    std::unique_lock<std::mutex> lock(mutex_);
    initReady_.wait(lock,[this]{ return initDone_; });
    const bool initialized = initOk_;
    const bool ok = initialized && frameReady_.wait_for(
        lock,std::chrono::seconds(3),[this]{ return hasNewFrame_ || stopped_; }) && hasNewFrame_;
    lock.unlock();
    if(!ok && initialized) qWarning() << "WGC produced no startup frame within 3 seconds";
    if(!ok) Close();
    return ok;
}

// 设置停止标志，并唤醒等待帧的调用线程与等待下一次采集的工作线程。
void WGCScreenCapture::RequestStop()
{
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    frameReady_.notify_all();
    workerWake_.notify_all();
}

// 请求停止、等待工作线程退出，再清空当前保存的帧。
bool WGCScreenCapture::Close()
{
    RequestStop();
    if(worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    latestFrame_ = VideoFrame{};
    hasNewFrame_ = false;
    initOk_ = false;
    return true;
}

// 阻塞到有新帧或采集停止。帧内的共享指针（CPU owner 或 GPU 句柄）使其数据在调用方使用期间有效。
bool WGCScreenCapture::WaitLatestFrame(VideoFrame& frame)
{
    std::unique_lock<std::mutex> lock(mutex_);
    frameReady_.wait(lock,[this]{ return hasNewFrame_ || stopped_; });
    if(stopped_) return false;
    hasNewFrame_ = false;
    frame = latestFrame_;
    if(frame.kind == VideoFrameKind::Cpu && frame.cpu.owner)
    {
        frame.cpu.data = frame.cpu.owner->data();
    }
    return true;
}

// 覆盖单槽位中的旧帧，更新序号和时间戳，然后通知一个取帧线程。
void WGCScreenCapture::Publish(const VideoFrame& frame, quint64 sequence)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_) return;
        latestFrame_ = frame;
        latestFrame_.sequence = sequence;
        latestFrame_.capturedAt = std::chrono::steady_clock::now();
        width_.store(frame.width);
        height_.store(frame.height);
        captureSequence_.store(sequence);
        hasNewFrame_ = true;
    }
    frameReady_.notify_one();
}

// 在工作线程初始化 WinRT，持续取帧并发布；显示器变化或读取失败时重建采集会话。
void WGCScreenCapture::Run()
{
    const HRESULT initHr = RoInitialize(RO_INIT_MULTITHREADED);
    const bool gpuOutput = (captureOutput_.load() == CaptureOutput::GpuTexture);
    D3D11SharedContext* shared = externalDevice_.load();
    std::unique_ptr<WgcState> state;
    if(SUCCEEDED(initHr))
    {
        state.reset(new WgcState());
        state->gpuTexture = gpuOutput;
        state->shared = shared;
        if(!state->Open()) state.reset();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initOk_ = static_cast<bool>(state);
        if(state)
        {
            width_.store(static_cast<quint32>(state->poolSize.Width));
            height_.store(static_cast<quint32>(state->poolSize.Height));
        }
        initDone_ = true;
    }
    initReady_.notify_one();
    if(!state)
    {
        if(FAILED(initHr)) qWarning() << "WGC RoInitialize failed" << Qt::hex << initHr;
        if(SUCCEEDED(initHr)) RoUninitialize();
        return;
    }

    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::nanoseconds(1000000000 / kTargetFramerate);
    auto nextTick = Clock::now();
    Clock::time_point firstPublished;
    bool hasPublished = false;
    VideoFrame lastFrame;
    auto nextMonitorCheck = Clock::now() + std::chrono::seconds(1);
    while(!stop_.load())
    {
        const auto now = Clock::now();
        if(now >= nextMonitorCheck)
        {
            nextMonitorCheck = now + std::chrono::seconds(1);
            if(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY) != state->monitor)
            {
                qInfo() << "WGC primary monitor changed; restarting capture session";
                state.reset();
                lastFrame = VideoFrame{};
            }
        }
        if(!state)
        {
            state.reset(new WgcState());
            state->gpuTexture = gpuOutput;
            state->shared = shared;
            if(!state->Open())
            {
                state.reset();
                lastFrame = VideoFrame{};
                std::unique_lock<std::mutex> lock(mutex_);
                workerWake_.wait_for(lock,std::chrono::milliseconds(500),
                                     [this]{ return stop_.load(); });
                continue;
            }
        }

        const ReadResult result = state->Read(lastFrame);
        if(result == ReadResult::error)
        {
            qWarning() << "WGC frame read failed; restarting capture session";
            state.reset();
            lastFrame = VideoFrame{};
            continue;
        }
        // 帧池暂时无新帧时沿用上一帧，按目标帧率继续发布递增序号。
        const bool hasFrame = (lastFrame.kind == VideoFrameKind::Gpu) ? static_cast<bool>(lastFrame.gpu)
                                                                     : static_cast<bool>(lastFrame.cpu.owner);
        if(hasFrame)
        {
            const auto publishAt = Clock::now();
            if(!hasPublished)
            {
                firstPublished = publishAt;
                hasPublished = true;
            }
            const quint64 clockSequence = static_cast<quint64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(publishAt - firstPublished).count()
                / interval.count()) + 1;
            const quint64 sequence = (std::max)(captureSequence_.load() + 1,clockSequence);
            Publish(lastFrame,sequence);
        }
        nextTick += interval;
        if(nextTick < Clock::now()) nextTick = Clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        workerWake_.wait_until(lock,nextTick,[this]{ return stop_.load(); });
    }
    state.reset();
    RoUninitialize();
}
