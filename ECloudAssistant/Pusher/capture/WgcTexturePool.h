#ifndef WGC_TEXTURE_POOL_H
#define WGC_TEXTURE_POOL_H

#include <array>
#include <memory>
#include <utility>
#include <d3d11.h>
#include <wrl/client.h>

// 仅采集线程创建池、申请槽位；消费者只持有/释放 Slot 的共享引用。
// 不向消费者暴露池或 weak_ptr，引用计数为 1 时不可能再产生旧帧的持有者。
class WgcTexturePool
{
public:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
    };
    static constexpr size_t Capacity = 3;

    WgcTexturePool() = default;
    WgcTexturePool(const WgcTexturePool&) = delete;
    WgcTexturePool& operator=(const WgcTexturePool&) = delete;

    HRESULT Ensure(ID3D11Device* device, D3D11_TEXTURE2D_DESC desc)
    {
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        if(slots_[0] && slots_[0]->device.Get() == device &&
           desc.Width == desc_.Width && desc.Height == desc_.Height &&
           desc.Format == desc_.Format && desc.MipLevels == desc_.MipLevels &&
           desc.ArraySize == desc_.ArraySize &&
           desc.SampleDesc.Count == desc_.SampleDesc.Count &&
           desc.SampleDesc.Quality == desc_.SampleDesc.Quality)
        {
            return S_OK;
        }

        // 全部创建成功才换代；失败自动释放半成品，旧池和在途帧仍然有效。
        std::array<std::shared_ptr<Slot>,Capacity> next;
        for(auto& slot : next)
        {
            slot = std::make_shared<Slot>();
            slot->device = device;
            const HRESULT hr = device->CreateTexture2D(&desc,nullptr,slot->texture.GetAddressOf());
            if(FAILED(hr)) return hr;
        }
        slots_ = std::move(next);
        desc_ = desc;
        return S_OK;
    }

    std::shared_ptr<Slot> Acquire()
    {
        for(const auto& slot : slots_)
        {
            if(slot && slot.use_count() == 1) return slot;
        }
        return nullptr;
    }

private:
    D3D11_TEXTURE2D_DESC desc_{};
    std::array<std::shared_ptr<Slot>,Capacity> slots_;
};

#endif // WGC_TEXTURE_POOL_H
