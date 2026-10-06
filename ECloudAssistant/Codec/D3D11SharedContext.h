#ifndef D3D11_SHARED_CONTEXT_H
#define D3D11_SHARED_CONTEXT_H

#include <QtGlobal>
#include <QString>
#include <memory>
#include <mutex>
#include <d3d11.h>
#include <wrl/client.h>

// 采集与硬件编码共用的 D3D11 设备。
// WGC 产出的帧纹理与 NVENC 必须在同一个 device 上，纹理才能被 VideoProcessorBlt 消费，
// 因此由推流管理器统一创建，再同时注入采集侧（WGC）与编码侧（HardwareVideoEncoder）。
// 立即上下文不是线程安全的：采集线程在上面 CopyResource，编码线程在上面做格式转换，
// 所有使用点都必须持 ContextLock()。
class D3D11SharedContext
{
public:
    // preferredVendorId 为 0 时用默认 adapter，否则优先选该厂商（NVENC 传 0x10DE）；
    // 指定厂商不存在时退回默认 adapter。失败返回 nullptr。
    static std::unique_ptr<D3D11SharedContext> Create(quint32 preferredVendorId = 0);
    ~D3D11SharedContext();
    D3D11SharedContext(const D3D11SharedContext&) = delete;
    D3D11SharedContext& operator=(const D3D11SharedContext&) = delete;

    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    // 可能为空：adapter 不支持 video 接口时
    ID3D11VideoDevice* videoDevice() const { return videoDevice_.Get(); }
    ID3D11VideoContext* videoContext() const { return videoContext_.Get(); }

    std::mutex& ContextLock() { return contextMutex_; }

    quint32 adapterVendorId() const { return adapterVendorId_; }
    QString adapterName() const { return adapterName_; }

private:
    D3D11SharedContext() = default;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
    quint32 adapterVendorId_ = 0;
    QString adapterName_;
    std::mutex contextMutex_;
};

#endif // D3D11_SHARED_CONTEXT_H
