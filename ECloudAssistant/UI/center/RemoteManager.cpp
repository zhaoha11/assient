#include "RemoteManager.h"
#include <QDebug>

RemoteManager::~RemoteManager()
{
    Close();
}

RemoteManager::RemoteManager()
    :pullerWgt_(nullptr)
    ,event_loop_(nullptr)
    ,sig_conn_(nullptr)
{
    //创建事件循环
    event_loop_.reset(new EventLoop(2));
    // 信号由编码线程发出，用队列连接投递到本对象所在线程执行
    connect(this,&RtmpPushManager::videoPathFailed,this,&RemoteManager::HandleVideoPathFailed,
            Qt::QueuedConnection);
}

void RemoteManager::Init(const QString &sigIp, uint16_t port,const QString& code,const DeviceStatusCallback& statusCallback)
{
    if(statusCallback) statusCallback(QString::fromUtf8("正在注册设备"),QString::fromUtf8("正在连接信令服务器。"),"pending");
    //连接这个信令服务器
    //创建tcp连接
    TcpSocket tcp_socket;
    tcp_socket.Create();
    if(!tcp_socket.Connect(sigIp.toStdString(),port))
    {
        qDebug() << "连接信令服务器失败"; if(statusCallback) statusCallback(QString::fromUtf8("连接异常"),QString::fromUtf8("无法连接信令服务器，请检查网络或服务器状态。"),"error");
        return;
    }
    qDebug() << "连接信令服务器成功";
    //生成一个信令连接器
    sig_conn_.reset(new SigConnection(event_loop_->GetTaskSchduler().get(),tcp_socket.GetSocket(),code));//默认被控端
    sig_conn_->SetJoinResultCallBack([statusCallback](bool ok){if(statusCallback)statusCallback(ok?QString::fromUtf8("可被远程连接"):QString::fromUtf8("注册失败"),ok?QString::fromUtf8("信令服务器已确认设备身份，其他客户端可使用设备识别码发起连接。"):QString::fromUtf8("信令服务器未确认设备注册，请重新登录后再试。"),ok?"ready":"error");});
    sig_conn_->SetStopStreamCallBack([this](){
        this->HandleStopStream();
    });
    sig_conn_->SetStartStreamCallBack([this](const QString& streamAddr, uint8_t captureBackend){
        return this->HandleStartStream(streamAddr,captureBackend);
    });
    if(sig_conn_->Start() != 0)
    {
        qDebug() << "加入信令服务器失败"; if(statusCallback) statusCallback(QString::fromUtf8("注册失败"),QString::fromUtf8("无法发送设备注册请求，请重新登录。"),"error");
        sig_conn_.reset();
        return;
    }
    return;
}

void RemoteManager::StartRemote(const QString &sigIp, uint16_t port, const QString &code)
{
    //开始远程
    pullerWgt_.reset(new PullerWgt(event_loop_.get(),nullptr));
    pullerWgt_->show();
    //把本机下拉框的采集方式作为请求发给信令：0=GDI、1=WGC、2=摄像头，由被控端按信令决定视频源
    const uint8_t requestedBackend = GetVideoSourceKind() == VideoSourceKind::Camera
                                         ? 2 : (GetCaptureBackend() == CaptureBackend::WGC ? 1 : 0);
    //创建一个拉流器开始连接
    if(!pullerWgt_->Connect(sigIp,port,code,requestedBackend))
    {
        qDebug() << "远程连接失败";
        return;
    }
    qDebug() << "远程连接成功";
}

void RemoteManager::HandleStopStream()
{
    //停止推流
    lastStreamAddr_.clear();
    RtmpPushManager::Close();
}

bool RemoteManager::HandleStartStream(const QString &streamAddr, uint8_t captureBackend)
{
    //开始推流
    lastStreamAddr_ = streamAddr;
    // 信令优先：控制端下发的采集方式决定本会话视频源，覆盖被控端本地下拉的默认值。
    // 0=GDI、1=WGC、2=摄像头；须在 Open 之前设定，Init 会读取 videoSourceKind_。
    switch(captureBackend)
    {
    case 2:
        SetVideoSourceKind(VideoSourceKind::Camera);
        break;
    case 1:
        SetVideoSourceKind(VideoSourceKind::Screen);
        SetCaptureBackend(CaptureBackend::WGC);
        break;
    default:
        SetVideoSourceKind(VideoSourceKind::Screen);
        SetCaptureBackend(CaptureBackend::GDI);
        break;
    }
    const bool opened = this->Open(streamAddr);
    if(opened)
    {
        SetCaptureBackend(GetActiveCaptureBackend());
        emit captureBackendStarted(ActiveBackendValue());
    }
    return opened;
}

// 当前活跃视频源的对外编号，与 RemoteWgt 下拉框索引对齐（0=GDI、1=WGC、2=摄像头）
int RemoteManager::ActiveBackendValue() const
{
    if(GetActiveVideoSourceKind() == VideoSourceKind::Camera)
    {
        return 2;
    }
    return GetActiveCaptureBackend() == CaptureBackend::WGC ? 1 : 0;
}

// 会话级退化：不在某一帧上换编码器，而是停掉当前推流、以软编路径整条重建。只降一级。
void RemoteManager::HandleVideoPathFailed()
{
    if(lastStreamAddr_.isEmpty())
    {
        return;
    }
    // 摄像头路径本身就是软编：设备或编码失败重建同一个摄像头没有意义，避免空转
    if(GetActiveVideoSourceKind() == VideoSourceKind::Camera)
    {
        qWarning() << "[PUSH] camera video path failed, not rebuilding";
        return;
    }
    qWarning() << "[PUSH] video path failed at runtime, rebuilding session with software encoder";
    RtmpPushManager::Close();
    // 强制软编：重开时 Init 会跳过 GPU 直通档，落到 CPU 读回 + 软编
    SetEncoderKind(VideoEncoderKind::Software);
    const bool opened = this->Open(lastStreamAddr_);
    // 恢复默认，下次启动仍会优先尝试 GPU 直通（硬编若仍坏会再次退化）
    SetEncoderKind(VideoEncoderKind::Hardware);
    if(opened)
    {
        SetCaptureBackend(GetActiveCaptureBackend());
        emit captureBackendStarted(ActiveBackendValue());
    }
    else
    {
        qWarning() << "[PUSH] software rebuild failed, push stopped";
    }
}

void RemoteManager::Close()
{
    if(sig_conn_ && sig_conn_->isPusher())
    {
        //停止推流
        RtmpPushManager::Close();
    }
}
