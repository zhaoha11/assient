#ifndef SIGCONNECTION_H
#define SIGCONNECTION_H
#include<functional>
#include "BufferReader.h"
#include "TcpConnection.h"
#include <QtGlobal>
#include <QScreen>
#include <QCursor>
#include "AV_Queue.h"

struct packet_head;
class SigConnection : public TcpConnection
{
public:
    enum UserType
    {
        CONTROLLED,//被控端
        CONTROLLING //控制端
    };
    enum State
    {
        NONE,
        IDLE,
        PULLER,
        PUSHER
    };
    SigConnection(TaskScheduler* scheduler, int sockfd,const QString& code,const UserType& type = CONTROLLED);
    virtual ~SigConnection();
    qint32 Start();
    inline bool isIdle(){return state_ == IDLE;}
    inline bool isPusher(){return state_ == PUSHER;}
    inline bool isPuller(){return state_ == PULLER;}
    inline bool isNone(){return state_ == NONE;}
    using JoinResultCallBack = std::function<void(bool)>;
    using StopStreamCallBack = std::function<void()>;
    using StartStreamCallBack = std::function<bool(const QString& streamAddr)>;
    inline void SetJoinResultCallBack(const JoinResultCallBack& cb){joinResultCb_=cb;}
    inline void SetStartStreamCallBack(const StartStreamCallBack& cb){startStreamCb_ = cb;}
    inline void SetStopStreamCallBack(const StopStreamCallBack& cb){stopStreamCb_ = cb;}
protected:
    bool OnRead(BufferReader& buffer);
    void OnClose();
    void HandleMessage(BufferReader& buffer);
private:
    static QString GenerateControlSessionCode();
    qint32 Join();
    qint32 obtainStream();
    void doJoin(const packet_head* data);
    void doObtainStreamReply(const packet_head* data);
    void doPlayStream(const packet_head* data);
    void doCtreatStream(const packet_head* data);
    void doDeleteStream(const packet_head* data);
private:
    void doMouseEvent(const packet_head* data);
    void doMouseMoveEvent(const packet_head* data);
    void doKeyEvent(const packet_head* data);
    void doWheelEvent(const packet_head* data);
private:
    bool quit_ = false;
    State state_;
    // Identity used by this TCP connection when it joins SigServer.
    QString joinCode_ = "";
    // Target device USER_CODE, used only by controlling connections.
    QString targetCode_ = "";
    int joinRetryCount_ = 0;
    const UserType type_;
    QScreen* screen_ = nullptr;
    JoinResultCallBack joinResultCb_=[](bool){};
    StopStreamCallBack stopStreamCb_ = [](){};
    StartStreamCallBack startStreamCb_ = [](const QString&)->bool{return true;};
    std::unique_ptr<std::thread> eventthread_ = nullptr;
};
#endif // SIGCONNECTION_H
