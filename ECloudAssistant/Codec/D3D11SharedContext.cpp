#include "D3D11SharedContext.h"

#include <QDebug>
#include <dxgi.h>

using Microsoft::WRL::ComPtr;

D3D11SharedContext::~D3D11SharedContext() = default;

// 先按厂商挑 adapter（NVENC 在独显上，默认 adapter 往往是核显），再在其上建 device。
// device 必须带 VIDEO_SUPPORT：VideoProcessor 的 blt 依赖 ID3D11VideoDevice/Context。
std::unique_ptr<D3D11SharedContext> D3D11SharedContext::Create(quint32 preferredVendorId)
{
    std::unique_ptr<D3D11SharedContext> self(new D3D11SharedContext());

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.GetAddressOf()));
    if(FAILED(hr))
    {
        qWarning() << "[GPU] CreateDXGIFactory1 failed" << Qt::hex << hr;
        return nullptr;
    }

    ComPtr<IDXGIAdapter1> chosen;
    for(UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(i,adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        if(FAILED(adapter->GetDesc1(&desc))) continue;
        if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // 软渲染 adapter 不能做硬编
        if(preferredVendorId != 0 && desc.VendorId != preferredVendorId) continue;
        chosen = adapter;
        break;
    }
    if(!chosen && preferredVendorId != 0)
    {
        qWarning() << "[GPU] preferred adapter vendor" << Qt::hex << preferredVendorId
                   << "not found, falling back to default adapter";
    }

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    hr = D3D11CreateDevice(chosen.Get(),
                           chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                           nullptr,flags,nullptr,0,D3D11_SDK_VERSION,
                           self->device_.GetAddressOf(),nullptr,self->context_.GetAddressOf());
    if(FAILED(hr))
    {
        qWarning() << "[GPU] D3D11CreateDevice failed" << Qt::hex << hr;
        return nullptr;
    }

    // video 接口拿不到并不立刻失败：上层会在创建 VideoProcessor 时再判一次
    if(SUCCEEDED(self->device_.As(&self->videoDevice_)))
    {
        self->context_.As(&self->videoContext_);
    }

    // 记录 device 真正落在哪个 adapter 上（chosen 可能为空，以 device 反查为准）
    ComPtr<IDXGIDevice> dxgiDevice;
    if(SUCCEEDED(self->device_.As(&dxgiDevice)))
    {
        ComPtr<IDXGIAdapter> adapter;
        if(SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
        {
            DXGI_ADAPTER_DESC desc{};
            if(SUCCEEDED(adapter->GetDesc(&desc)))
            {
                self->adapterVendorId_ = desc.VendorId;
                self->adapterName_ = QString::fromWCharArray(desc.Description);
            }
        }
    }
    qInfo() << "[GPU] shared D3D11 device on adapter" << self->adapterName_
            << "vendor" << Qt::hex << self->adapterVendorId_
            << (self->videoDevice_ ? "video:ok" : "video:unavailable");
    return self;
}
