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
    sig_conn_->SetStartStreamCallBack([this](const QString& streamAddr){
        return this->HandleStartStream(streamAddr);
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
    //创建一个拉流器开始连接
    if(!pullerWgt_->Connect(sigIp,port,code))
    {
        qDebug() << "远程连接失败";
        return;
    }
    qDebug() << "远程连接成功";
}

void RemoteManager::HandleStopStream()
{
    //停止推流
    RtmpPushManager::Close();
}

bool RemoteManager::HandleStartStream(const QString &streamAddr)
{
    //开始推流
    qInfo() << "[TRACE-PLAY-20260814] RemoteManager starts push, url =" << streamAddr;
    qDebug() << "push: " << streamAddr;
    const bool opened = this->Open(streamAddr);
    qInfo() << "[TRACE-PLAY-20260814] RtmpPushManager::Open returned" << opened;
    return opened;
}

void RemoteManager::Close()
{
    if(sig_conn_ && sig_conn_->isPusher())
    {
        //停止推流
        RtmpPushManager::Close();
    }
}
