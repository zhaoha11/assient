#ifndef REMOTEMANAGER_H
#define REMOTEMANAGER_H
#include <functional>
#include "RtmpPushManager.h"
#include "PullerWgt.h"
#include "SigConnection.h"


class RemoteManager : public RtmpPushManager
{
    Q_OBJECT
public:
    using DeviceStatusCallback = std::function<void(const QString &, const QString &, const QString &)>;
    ~RemoteManager();
    RemoteManager();
    RemoteManager(RemoteManager &&) = delete;
    RemoteManager(const RemoteManager &) = delete;
    RemoteManager &operator=(RemoteManager &&) = delete;
    RemoteManager &operator=(const RemoteManager &) = delete;
public:
   void Init(const QString& sigIp,uint16_t port,const QString& code,const DeviceStatusCallback& statusCallback = {});
    void StartRemote(const QString& sigIp,uint16_t port,const QString& code);
signals:
    void captureBackendStarted(int backend);
protected:
    void HandleStopStream();
    bool HandleStartStream(const QString& streamAddr, uint8_t captureBackend);
    // 运行中视频编码路径失败：停止当前推流并以软编路径重建一次会话
    void HandleVideoPathFailed();
private:
    void Close();
private:
    std::unique_ptr<PullerWgt> pullerWgt_;
    std::unique_ptr<EventLoop> event_loop_;
    std::shared_ptr<SigConnection> sig_conn_;
    // 记住最近一次推流地址，运行中退化重建会话时要用它重开
    QString lastStreamAddr_;
};
#endif // REMOTEMANAGER_H
