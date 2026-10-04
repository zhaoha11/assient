#include "WGCScreenCapture.h"
#include "AV_Common.h"

#include <QDebug>
#include <algorithm>
#include <cstring>
#include <memory>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
// Qt's MinGW 13.1 SDK has the original WGC session header but not the frame-pool
// interfaces. These are the small ABI surface used here, matching the Windows
// Graphics Capture and D3D11 interop interfaces in the Windows SDK.
struct WgcSize { INT32 width; INT32 height; };
struct WgcFrame;
struct WgcSession;
struct WgcItem;

MIDL_INTERFACE("fa50c623-38da-4b32-acf3-fa9734ad800e") WgcFrame : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Surface(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SystemRelativeTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentSize(WgcSize* value) = 0;
};
MIDL_INTERFACE("814e42a9-f70f-4ad7-939b-fddcc6eb880d") WgcSession : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE StartCapture() = 0;
};
MIDL_INTERFACE("2c39ae40-7d2e-5044-804e-8b6799d4cf9e") WgcSession2 : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(boolean value) = 0;
};
MIDL_INTERFACE("2224a540-5974-49aa-b232-0882536f4cb5") WgcSessionStatics : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE IsSupported(boolean* result) = 0;
};
MIDL_INTERFACE("79c3f95b-31f7-4ec2-a464-632ef5d30760") WgcItem : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(WgcSize* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(void* handler, void* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(void* token) = 0;
};
MIDL_INTERFACE("3628e81b-3cac-4c60-b7f4-23ce0e0c3356") WgcItemInterop : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID iid, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID iid, void** result) = 0;
};
MIDL_INTERFACE("24eb6d22-1975-422e-82e7-780dbd8ddf24") WgcFramePool : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Recreate(IInspectable* device, INT32 format,
                                               INT32 buffers, WgcSize size) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetNextFrame(WgcFrame** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameArrived(void* handler, void* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameArrived(void* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCaptureSession(WgcItem* item, WgcSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(IInspectable** value) = 0;
};
MIDL_INTERFACE("589b103f-6bbc-5df5-a991-02e28b3b66d5") WgcFramePoolStatics2 : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateFreeThreaded(IInspectable* device, INT32 format,
                                                          INT32 buffers, WgcSize size,
                                                          WgcFramePool** result) = 0;
};
MIDL_INTERFACE("a9b3d012-3df2-4ee3-b8d1-8695f457d3c1") WgcDxgiAccess : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** result) = 0;
};
MIDL_INTERFACE("30d5a829-7fa4-4026-83bb-d75bae4ea99e") WgcClosable : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
} // namespace

// MinGW emulates __uuidof through explicit template specializations.
__CRT_UUID_DECL(WgcFrame,0xfa50c623,0x38da,0x4b32,0xac,0xf3,0xfa,0x97,0x34,0xad,0x80,0x0e)
__CRT_UUID_DECL(WgcSession,0x814e42a9,0xf70f,0x4ad7,0x93,0x9b,0xfd,0xdc,0xc6,0xeb,0x88,0x0d)
__CRT_UUID_DECL(WgcSession2,0x2c39ae40,0x7d2e,0x5044,0x80,0x4e,0x8b,0x67,0x99,0xd4,0xcf,0x9e)
__CRT_UUID_DECL(WgcSessionStatics,0x2224a540,0x5974,0x49aa,0xb2,0x32,0x08,0x82,0x53,0x6f,0x4c,0xb5)
__CRT_UUID_DECL(WgcItem,0x79c3f95b,0x31f7,0x4ec2,0xa4,0x64,0x63,0x2e,0xf5,0xd3,0x07,0x60)
__CRT_UUID_DECL(WgcItemInterop,0x3628e81b,0x3cac,0x4c60,0xb7,0xf4,0x23,0xce,0x0e,0x0c,0x33,0x56)
__CRT_UUID_DECL(WgcFramePool,0x24eb6d22,0x1975,0x422e,0x82,0xe7,0x78,0x0d,0xbd,0x8d,0xdf,0x24)
__CRT_UUID_DECL(WgcFramePoolStatics2,0x589b103f,0x6bbc,0x5df5,0xa9,0x91,0x02,0xe2,0x8b,0x3b,0x66,0xd5)
__CRT_UUID_DECL(WgcDxgiAccess,0xa9b3d012,0x3df2,0x4ee3,0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1)
__CRT_UUID_DECL(WgcClosable,0x30d5a829,0x7fa4,0x4026,0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e)

namespace
{

extern "C" HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(
    IDXGIDevice* dxgiDevice, IInspectable** graphicsDevice);

void CloseWinrt(IInspectable* object)
{
    if(!object) return;
    ComPtr<WgcClosable> closable;
    if(SUCCEEDED(object->QueryInterface(__uuidof(WgcClosable),
                                        reinterpret_cast<void**>(closable.GetAddressOf()))))
    {
        closable->Close();
    }
}

template<typename T>
HRESULT GetFactory(const wchar_t* name, ComPtr<T>& factory)
{
    HSTRING className = nullptr;
    HRESULT hr = WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &className);
    if(SUCCEEDED(hr))
    {
        hr = RoGetActivationFactory(className, __uuidof(T),
                                    reinterpret_cast<void**>(factory.GetAddressOf()));
        WindowsDeleteString(className);
    }
    return hr;
}

enum class ReadResult { none, frame, error };

struct WgcState
{
    HMONITOR monitor = nullptr;
    WgcSize poolSize{0,0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IInspectable> graphicsDevice;
    ComPtr<WgcItem> item;
    ComPtr<WgcFramePool> pool;
    ComPtr<WgcSession> session;
    ComPtr<ID3D11Texture2D> staging;
    UINT stagingWidth = 0;
    UINT stagingHeight = 0;

    ~WgcState()
    {
        CloseWinrt(session.Get());
        CloseWinrt(pool.Get());
    }

    bool Open()
    {
        ComPtr<WgcSessionStatics> sessionStatics;
        boolean supported = false;
        if(FAILED(GetFactory(L"Windows.Graphics.Capture.GraphicsCaptureSession", sessionStatics)) ||
           FAILED(sessionStatics->IsSupported(&supported)) || !supported)
        {
            qWarning() << "WGC is not supported on this desktop";
            return false;
        }

        const POINT primaryPoint{0,0};
        monitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);
        if(!monitor) return false;

        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,
                                       nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr))
        {
            qWarning() << "WGC D3D11CreateDevice failed" << Qt::hex << hr;
            return false;
        }
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device.As(&dxgiDevice);
        if(SUCCEEDED(hr))
        {
            hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                                         graphicsDevice.GetAddressOf());
        }
        if(FAILED(hr))
        {
            qWarning() << "WGC D3D11 interop failed" << Qt::hex << hr;
            return false;
        }

        ComPtr<WgcItemInterop> itemInterop;
        hr = GetFactory(L"Windows.Graphics.Capture.GraphicsCaptureItem",itemInterop);
        if(SUCCEEDED(hr))
        {
            hr = itemInterop->CreateForMonitor(monitor,__uuidof(WgcItem),
                                                reinterpret_cast<void**>(item.GetAddressOf()));
        }
        if(SUCCEEDED(hr)) hr = item->get_Size(&poolSize);
        if(FAILED(hr) || poolSize.width <= 0 || poolSize.height <= 0)
        {
            qWarning() << "WGC primary monitor item failed" << Qt::hex << hr;
            return false;
        }

        ComPtr<WgcFramePoolStatics2> poolStatics;
        hr = GetFactory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",poolStatics);
        if(SUCCEEDED(hr))
        {
            hr = poolStatics->CreateFreeThreaded(graphicsDevice.Get(),
                                                  DXGI_FORMAT_B8G8R8A8_UNORM,2,poolSize,
                                                  pool.GetAddressOf());
        }
        if(SUCCEEDED(hr)) hr = pool->CreateCaptureSession(item.Get(),session.GetAddressOf());
        if(FAILED(hr))
        {
            qWarning() << "WGC frame pool/session failed" << Qt::hex << hr;
            return false;
        }

        ComPtr<WgcSession2> cursorSession;
        if(SUCCEEDED(session.As(&cursorSession)))
        {
            cursorSession->put_IsCursorCaptureEnabled(true);
        }
        hr = session->StartCapture();
        if(FAILED(hr))
        {
            qWarning() << "WGC StartCapture failed" << Qt::hex << hr;
            return false;
        }
        qInfo() << "WGC primary monitor capture" << poolSize.width << "x" << poolSize.height;
        return true;
    }

    ReadResult Read(std::shared_ptr<std::vector<quint8>>& pixels,
                    quint32& width, quint32& height)
    {
        ComPtr<WgcFrame> newest;
        for(int i = 0; i < 4; ++i)
        {
            ComPtr<WgcFrame> frame;
            const HRESULT hr = pool->TryGetNextFrame(frame.GetAddressOf());
            if(FAILED(hr)) return ReadResult::error;
            if(!frame) break;
            CloseWinrt(newest.Get());
            newest = frame;
        }
        if(!newest) return ReadResult::none;

        WgcSize content{0,0};
        ComPtr<IInspectable> surface;
        ComPtr<WgcDxgiAccess> access;
        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = newest->get_ContentSize(&content);
        if(SUCCEEDED(hr)) hr = newest->get_Surface(surface.GetAddressOf());
        if(SUCCEEDED(hr)) hr = surface.As(&access);
        if(SUCCEEDED(hr))
        {
            hr = access->GetInterface(__uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(texture.GetAddressOf()));
        }
        if(FAILED(hr) || content.width <= 0 || content.height <= 0)
        {
            CloseWinrt(newest.Get());
            return ReadResult::error;
        }

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if(desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
           static_cast<UINT>(content.width) > desc.Width ||
           static_cast<UINT>(content.height) > desc.Height)
        {
            CloseWinrt(newest.Get());
            return ReadResult::error;
        }
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
                CloseWinrt(newest.Get());
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
            CloseWinrt(newest.Get());
            return ReadResult::error;
        }
        width = static_cast<quint32>(content.width);
        height = static_cast<quint32>(content.height);
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        if(mapped.RowPitch < rowBytes)
        {
            context->Unmap(staging.Get(),0);
            CloseWinrt(newest.Get());
            return ReadResult::error;
        }
        auto newPixels = std::make_shared<std::vector<quint8>>(rowBytes * height);
        for(quint32 y = 0; y < height; ++y)
        {
            std::memcpy(newPixels->data() + y * rowBytes,
                        static_cast<const quint8*>(mapped.pData) + y * mapped.RowPitch,rowBytes);
        }
        context->Unmap(staging.Get(),0);
        pixels = std::move(newPixels);
        texture.Reset();
        access.Reset();
        surface.Reset();
        CloseWinrt(newest.Get());
        newest.Reset();

        if(content.width != poolSize.width || content.height != poolSize.height)
        {
            poolSize = content;
            hr = pool->Recreate(graphicsDevice.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,2,poolSize);
            if(FAILED(hr)) return ReadResult::error;
            qInfo() << "WGC capture resized" << width << "x" << height;
        }
        return ReadResult::frame;
    }
};
} // namespace

WGCScreenCapture::~WGCScreenCapture()
{
    Close();
}

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

bool WGCScreenCapture::Close()
{
    RequestStop();
    if(worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    latestPixels_.reset();
    hasNewFrame_ = false;
    initOk_ = false;
    return true;
}

bool WGCScreenCapture::WaitLatestFrame(CaptureFrameView& frame)
{
    std::unique_lock<std::mutex> lock(mutex_);
    frameReady_.wait(lock,[this]{ return hasNewFrame_ || stopped_; });
    if(stopped_) return false;
    hasNewFrame_ = false;
    frame.owner = latestPixels_;
    frame.data = frame.owner->data();
    frame.width = latestWidth_;
    frame.height = latestHeight_;
    frame.stride = latestWidth_ * 4;
    frame.size = static_cast<quint32>(frame.owner->size());
    frame.sequence = latestSequence_;
    frame.capturedAt = latestCapturedAt_;
    return true;
}

void WGCScreenCapture::Publish(std::shared_ptr<const std::vector<quint8>> pixels, quint32 width,
                               quint32 height, quint64 sequence)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_) return;
        latestPixels_ = std::move(pixels);
        latestWidth_ = width;
        latestHeight_ = height;
        latestSequence_ = sequence;
        latestCapturedAt_ = std::chrono::steady_clock::now();
        width_.store(width);
        height_.store(height);
        captureSequence_.store(sequence);
        hasNewFrame_ = true;
    }
    frameReady_.notify_one();
}

void WGCScreenCapture::Run()
{
    const HRESULT initHr = RoInitialize(RO_INIT_MULTITHREADED);
    std::unique_ptr<WgcState> state;
    if(SUCCEEDED(initHr))
    {
        state.reset(new WgcState());
        if(!state->Open()) state.reset();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initOk_ = static_cast<bool>(state);
        if(state)
        {
            width_.store(static_cast<quint32>(state->poolSize.width));
            height_.store(static_cast<quint32>(state->poolSize.height));
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
    std::shared_ptr<std::vector<quint8>> lastPixels;
    quint32 lastWidth = 0;
    quint32 lastHeight = 0;
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
                lastPixels.reset();
            }
        }
        if(!state)
        {
            state.reset(new WgcState());
            if(!state->Open())
            {
                state.reset();
                std::unique_lock<std::mutex> lock(mutex_);
                workerWake_.wait_for(lock,std::chrono::milliseconds(500),
                                     [this]{ return stop_.load(); });
                continue;
            }
        }

        const ReadResult result = state->Read(lastPixels,lastWidth,lastHeight);
        if(result == ReadResult::error)
        {
            qWarning() << "WGC frame read failed; restarting capture session";
            state.reset();
            lastPixels.reset();
            continue;
        }
        if(lastPixels)
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
            const quint64 sequence = std::max(captureSequence_.load() + 1,clockSequence);
            Publish(lastPixels,lastWidth,lastHeight,sequence);
        }
        nextTick += interval;
        if(nextTick < Clock::now()) nextTick = Clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        workerWake_.wait_until(lock,nextTick,[this]{ return stop_.load(); });
    }
    state.reset();
    RoUninitialize();
}
