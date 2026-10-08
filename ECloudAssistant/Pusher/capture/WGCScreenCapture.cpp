#include "WGCScreenCapture.h"
#include "AV_Common.h"
#include "D3D11SharedContext.h"
#include "StatsWindow.h"
#include "WgcTexturePool.h"

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

// 一次 Read 的副产物：都在采集线程本地取值，不跨线程。
// drainedFrames / frameAgeUs 供帧率与帧龄统计；
// lockWaitUs / copyUs 只在 GPU 分支有值，用来判断采集与编码是否在抢同一把 D3D 上下文锁。
struct ReadTiming
{
    quint32 drainedFrames = 0;
    qint64 frameAgeUs = 0;
    quint64 lockWaitUs = 0;
    quint64 copyUs = 0;
};

// 微秒取整，供本文件里的耗时打点复用
quint64 ElapsedUs(std::chrono::steady_clock::time_point begin)
{
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - begin).count());
}

// 当前时间，单位 100ns，与 Direct3D11CaptureFrame::SystemRelativeTime 同口径（QPC）。
// 用商余拆分换算，避免 counter * 1e7 在长时间运行后溢出。
quint64 QpcNow100ns()
{
    static const quint64 frequency = []{
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<quint64>(f.QuadPart);
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const quint64 c = static_cast<quint64>(counter.QuadPart);
    return (c / frequency) * 10000000ULL + (c % frequency) * 10000000ULL / frequency;
}

// GPU 帧句柄：持有自有纹理和 device，保证编码线程使用期间纹理有效；
// 同时持有 device 引用，即使采集端先退出也不会让纹理落在已销毁的 device 上。
// 这里（连同 WgcState）是采集侧唯一解释 ID3D11 类型的地方，公共头只见 void*。
class WgcGpuFrame : public IGpuVideoFrame
{
public:
    WgcGpuFrame(std::shared_ptr<WgcTexturePool::Slot> slot,
                quint32 width, quint32 height)
        : slot_(std::move(slot))
        , width_(width)
        , height_(height)
    {
    }

    quint32 width() const override { return width_; }
    quint32 height() const override { return height_; }
    VideoPixelFormat format() const override { return VideoPixelFormat::Bgra8; }
    void* nativeTexture() const override { return slot_->texture.Get(); }
    void* nativeDevice() const override { return slot_->device.Get(); }

private:
    std::shared_ptr<WgcTexturePool::Slot> slot_;
    quint32 width_;
    quint32 height_;
};

struct WgcState
{
    bool gpuTexture = false;   // true：直接输出 GPU 纹理；false：读回 CPU BGRA
    // 上层注入的共享 device（可空）。非空时直接复用它，采集纹理才能与硬件编码器同 device。
    D3D11SharedContext* shared = nullptr;
    // FrameArrived 事件计数（诊断用）。回调只 fetch_add 这个原子、不碰 WgcState，
    // 因此即使它在采集对象析构途中仍在执行也不会踩到已释放的内存。
    std::shared_ptr<std::atomic<quint64>> frameArrived;
    HMONITOR monitor = nullptr;
    winrt::Windows::Graphics::SizeInt32 poolSize{0,0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    wgd::IDirect3DDevice graphicsDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    ComPtr<ID3D11Texture2D> staging;
    WgcTexturePool bgraPool;
    UINT stagingWidth = 0;
    UINT stagingHeight = 0;
    bool ageProbeLogged = false;   // 帧龄口径只打一次原始值

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

            // 帧池缓冲数：可调，是「FrameArrived 计数不到」的那部分丢帧的唯一探针。
            // 池满后 WGC 直接丢弃新帧且不计数，所以只有放大这个数才能验证池溢出是否在吃帧率。
            constexpr int kFramePoolBuffers = 2;
            auto poolFactory = winrt::get_activation_factory<wgc::IDirect3D11CaptureFramePoolStatics2>(
                winrt::name_of<wgc::Direct3D11CaptureFramePool>());
            pool = poolFactory.CreateFreeThreaded(
                graphicsDevice,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                kFramePoolBuffers,poolSize);
            session = pool.CreateCaptureSession(item);
            session.IsCursorCaptureEnabled(true);
            // 只计数、不取帧：把「WGC 到底产了多少帧」与「我们 drain 了多少」分开，
            // 从而判断丢失的帧是 Windows 没给，还是我们自己的轮询/交接弄丢的。
            pool.FrameArrived([count = frameArrived](auto&&, auto&&)
            {
                if(count) count->fetch_add(1,std::memory_order_relaxed);
            });
            session.StartCapture();
            qInfo() << "WGC primary monitor capture" << poolSize.Width << "x" << poolSize.Height
                    << "buffers =" << kFramePoolBuffers
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
        pool.Recreate(graphicsDevice,
                      winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                      2,content);
        poolSize = content;
        qInfo() << "WGC capture resized" << width << "x" << height;
    }

    // 一次最多取四帧，使用本次取到的最后一帧。
    // gpuTexture 为真时把纹理复制进自有的默认用法纹理，产出 GPU 帧；
    // 否则经 staging 读回紧凑 BGRA，产出 CPU 帧。
    // 无帧、取得新帧、读取失败分别返回 none、frame、error。
    // drainedFrames 回传本次从池里实际取走的帧数：一次最多取四帧，较旧的被主动丢弃，
    // 但它们确实是 WGC 产出的源帧，必须计数，否则源帧率会被少算成「拿到过帧的轮询次数」。
    // frameAgeUs 回传所留帧的帧龄（合成器渲染该帧 到 现在），仅在返回 frame 时有效；
    // 负值表示时钟口径异常，如实上报由调用方单独计数，不截零。
    // lockWaitUs / copyUs 只在 GPU 分支填写：等锁时长与 CopyResource 调用耗时。
    ReadResult Read(VideoFrame& out, ReadTiming& timing)
    {
        timing = ReadTiming();
        quint32& drainedFrames = timing.drainedFrames;
        qint64& frameAgeUs = timing.frameAgeUs;
        wgc::Direct3D11CaptureFrame newest{nullptr};
        try
        {
            // 一次最多取四帧，丢弃本次取到的较旧帧。
            for(int i = 0; i < 4; ++i)
            {
                auto frame = pool.TryGetNextFrame();
                if(!frame) break;
                ++drainedFrames;
                if(newest) newest.Close();
                newest = std::move(frame);
            }
            if(!newest) return ReadResult::none;

            // 帧龄必须在这里采样：放到 CopyResource / Close 之后会把复制与锁等待也算进去。
            // 两个值同为 QPC 口径的 100ns 计数，直接相减即为「合成到取帧」的年龄。
            {
                const qint64 rendered100ns = static_cast<qint64>(newest.SystemRelativeTime().count());
                const qint64 now100ns = static_cast<qint64>(QpcNow100ns());
                frameAgeUs = (now100ns - rendered100ns) / 10;
                if(!ageProbeLogged)
                {
                    // 首帧打一次原始值，用来人工确认两个时钟确实同源（差应接近本帧真实年龄）
                    qInfo() << "[CAP-STATS] first frame age probe, sr100ns =" << rendered100ns
                            << "qpc100ns =" << now100ns << "ageUs =" << frameAgeUs;
                    ageProbeLogged = true;
                }
            }

            const auto content = newest.ContentSize();
            // GPU 路径丢弃尺寸过渡帧，先释放 WGC 帧再 Recreate。
            // 放大时 ContentSize 可能已超过旧 surface，不能先按旧纹理校验并报错。
            if(gpuTexture && content.Width > 0 && content.Height > 0 &&
               (content.Width != poolSize.Width || content.Height != poolSize.Height))
            {
                newest.Close();
                newest = nullptr;
                MaybeRecreatePool(content,static_cast<quint32>(content.Width),
                                  static_cast<quint32>(content.Height));
                return ReadResult::none;
            }
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
                // 固定三槽：只有最后一个帧持有者释放后才允许覆盖，不等待、不临时扩容。
                hr = bgraPool.Ensure(device.Get(),desc);
                if(FAILED(hr))
                {
                    qWarning() << "WGC BGRA texture pool creation failed" << Qt::hex << hr;
                    newest.Close();
                    return ReadResult::error;
                }
                auto slot = bgraPool.Acquire();
                if(!slot)
                {
                    newest.Close();
                    return ReadResult::none;
                }
                {
                    const std::chrono::steady_clock::time_point lockBegin = std::chrono::steady_clock::now();
                    std::unique_lock<std::mutex> contextLock = lockContext();
                    timing.lockWaitUs = ElapsedUs(lockBegin);
                    const std::chrono::steady_clock::time_point copyBegin = std::chrono::steady_clock::now();
                    // 与编码侧 VideoProcessorBlt 使用同一立即上下文/锁。
                    // 上一次读取命令先于本次覆盖提交，无需在 CPU 上等待 GPU 完成。
                    context->CopyResource(slot->texture.Get(),texture.Get());
                    timing.copyUs = ElapsedUs(copyBegin);
                }

                out = VideoFrame{};
                out.kind = VideoFrameKind::Gpu;
                out.gpu = std::make_shared<WgcGpuFrame>(std::move(slot),width,height);
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
            try { if(newest) newest.Close(); } catch(const winrt::hresult_error&) {}
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
    sourceFrames_.store(0);
    publishedFrames_.store(0);
    //每次会话换一个新的计数器：旧的回调可能还没跑完，不能复用同一个原子
    frameArrived_ = std::make_shared<std::atomic<quint64>>(0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = false;
        hasNewFrame_ = false;
        initDone_ = false;
        initOk_ = false;
        latestFrame_ = VideoFrame{};
    }
    worker_ = std::thread([this]
    {
        try { Run(); }
        catch(...)
        {
            qWarning() << "WGC worker failed unexpectedly; stopping capture";
            RequestStop();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initOk_ = false;
                initDone_ = true;
                latestFrame_ = VideoFrame{};
                hasNewFrame_ = false;
            }
            initReady_.notify_all();
        }
    });
    std::unique_lock<std::mutex> lock(mutex_);
    initReady_.wait(lock,[this]{ return initDone_; });
    const bool initialized = initOk_;
    const bool ok = initialized && frameReady_.wait_for(
        lock,std::chrono::seconds(3),[this]{ return hasNewFrame_ || stopped_; }) && hasNewFrame_ && !stopped_;
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

// 覆盖单槽位中的旧帧，更新时间戳后通知一个取帧线程。序号由 Run 按时间格算好传入。
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
        hasNewFrame_ = true;
    }
    frameReady_.notify_one();
}

// 在工作线程初始化 WinRT，持续取帧并发布；显示器变化或读取失败时重建采集会话。
void WGCScreenCapture::Run()
{
    const HRESULT initHr = RoInitialize(RO_INIT_MULTITHREADED);
    // 异常展开时也必须在所有局部 WGC 对象销毁后解除 WinRT 初始化。
    struct RoScope
    {
        HRESULT result;
        ~RoScope() { if(SUCCEEDED(result)) RoUninitialize(); }
    } roScope{initHr};
    const bool gpuOutput = (captureOutput_.load() == CaptureOutput::GpuTexture);
    D3D11SharedContext* shared = externalDevice_.load();
    std::unique_ptr<WgcState> state;
    if(SUCCEEDED(initHr))
    {
        state.reset(new WgcState());
        state->gpuTexture = gpuOutput;
        state->shared = shared;
        state->frameArrived = frameArrived_;
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
        return;
    }

    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::nanoseconds(1000000000 / kTargetFramerate);
    auto nextTick = Clock::now();
    //时间轴起点与 nextTick 同源，Publish 的序号即相对它量化到 1/目标帧率 的时间格
    const Clock::time_point sessionStart = Clock::now();
    quint64 lastSequence = 0;
    VideoFrame lastFrame;
    auto nextMonitorCheck = Clock::now() + std::chrono::seconds(1);
    //采集侧独立统计：本行每秒自打，不依赖编码完成，编码卡住时采集指标仍然可见。
    auto capStatsBegin = Clock::now();
    auto nextCapStats = capStatsBegin + std::chrono::seconds(1);
    quint64 lastReportSource = 0;
    quint64 lastReportPublished = 0;
    quint64 lastReportArrived = 0;
    quint64 negativeAges = 0;     //帧龄为负的次数，与符号无关地单独计数
    //帧龄可能为负，必须有符号窗口，否则负值会被折叠成巨大正数、min 也失去意义
    SampleWindow<qint64> ageWindow;
    UsWindow lockWaitWindow;      //等 ContextLock 的时长
    UsWindow copyWindow;          //CopyResource 调用耗时
    while(!stop_.load())
    {
        const auto now = Clock::now();
        if(now >= nextCapStats)
        {
            const quint64 elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                          now - capStatsBegin).count();
            const quint64 source = sourceFrames_.load();
            const quint64 published = publishedFrames_.load();
            const quint64 arrived = frameArrived_ ? frameArrived_->load() : 0;
            const quint64 sourceDelta = source - lastReportSource;
            const quint64 publishedDelta = published - lastReportPublished;
            const quint64 droppedDelta = sourceDelta > publishedDelta ? sourceDelta - publishedDelta : 0;
            const quint64 arrivedDelta = arrived >= lastReportArrived ? arrived - lastReportArrived : 0;
            const double perSecond = elapsedUs ? 1000000.0 / elapsedUs : 0.0;
            qInfo() << QString("[CAP-STATS] 源帧率 = %1 发布帧率 = %2 FrameArrived率 = %3 丢旧帧 = %4"
                               " 帧龄最小us = %5 帧龄均值us = %6 帧龄P95us = %7 帧龄负值 = %8"
                               " 取锁均值us = %9 取锁P95us = %10 Copy均值us = %11")
                       .arg(sourceDelta * perSecond, 0, 'f', 1)
                       .arg(publishedDelta * perSecond, 0, 'f', 1)
                       .arg(arrivedDelta * perSecond, 0, 'f', 1)
                       .arg(droppedDelta)
                       .arg(ageWindow.Min())
                       .arg(ageWindow.Mean())
                       .arg(ageWindow.Percentile(0.95))
                       .arg(negativeAges)
                       .arg(lockWaitWindow.Mean())
                       .arg(lockWaitWindow.Percentile(0.95))
                       .arg(copyWindow.Mean());
            lastReportSource = source;
            lastReportPublished = published;
            lastReportArrived = arrived;
            ageWindow.Reset();
            lockWaitWindow.Reset();
            copyWindow.Reset();
            capStatsBegin = now;
            nextCapStats = now + std::chrono::seconds(1);
        }
        if(now >= nextMonitorCheck)
        {
            nextMonitorCheck = now + std::chrono::seconds(1);
            if(state && MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY) != state->monitor)
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
            state->frameArrived = frameArrived_;
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

        ReadTiming timing;
        const ReadResult result = state->Read(lastFrame,timing);
        //从池里取走的都算源帧，含本次被丢弃的较旧帧；错误前吞掉的也算，它们已离开池
        sourceFrames_ += timing.drainedFrames;
        if(result == ReadResult::error)
        {
            qWarning() << "WGC frame read failed; restarting capture session";
            state.reset();
            lastFrame = VideoFrame{};
            continue;
        }
        //帧龄只在真的取到新帧时有效；负值照样进有符号窗口，另单独计数看异常比例
        if(result == ReadResult::frame)
        {
            ageWindow.Add(timing.frameAgeUs);
            if(timing.frameAgeUs < 0)
            {
                ++negativeAges;
            }
            lockWaitWindow.Add(timing.lockWaitUs);
            copyWindow.Add(timing.copyUs);
        }
        // 帧池暂时无新帧时沿用上一帧，按目标帧率继续发布递增序号。
        const bool hasFrame = (lastFrame.kind == VideoFrameKind::Gpu) ? static_cast<bool>(lastFrame.gpu)
                                                                     : static_cast<bool>(lastFrame.cpu.owner);
        if(hasFrame)
        {
            const auto publishAt = Clock::now();
            //序号由单调时钟量化而来，两帧过近落进同一格时强制递增，保证严格单调
            quint64 sequence = MakeSequence(publishAt,sessionStart);
            if(sequence <= lastSequence)
            {
                sequence = lastSequence + 1;
            }
            lastSequence = sequence;
            //只有真的发布了一张新画面才计入发布数；复用上一帧补节拍不算
            if(result == ReadResult::frame)
            {
                ++publishedFrames_;
            }
            Publish(lastFrame,sequence);
        }
        nextTick += interval;
        if(nextTick < Clock::now()) nextTick = Clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        workerWake_.wait_until(lock,nextTick,[this]{ return stop_.load(); });
    }
    state.reset();
}
