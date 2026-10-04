#include "TcpSocket.h"
#include "AVPlayer.h"
#include "EventLoop.h"
#include "SigConnection.h"
#include "VideoPullStats.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include "defin.h"
#include <QResizeEvent>
#include <chrono>

AVPlayer::~AVPlayer()
{
    Close();
}

AVPlayer::AVPlayer(EventLoop* loop,QWidget *parent)
    :OpenGLRender(parent)
    ,loop_(loop)
{
    setFocus();
    //设置窗口属性
    this->resize(parentWidget()->size());
    //设置无边框
    this->setWindowFlags(Qt::FramelessWindowHint);
    //背景颜色
    this->setAttribute(Qt::WA_StyledBackground);
    Init();
}

void AVPlayer::Init()
{
    //准备一个音频上下文
    avContext_ = new AVContext();
    //创建这个解封装器
    avDEMuxer_.reset(new AVDEMuxer(avContext_));
    avDEMuxer_->SetStreamCallBack([](bool){});
    //初始化这个音频播放器
    this->InitAudio(2,44100,16);
    //绑定信号与槽 去播放视频：跨线程用队列连接投递不含帧的刷新通知
    connect(this,&AVPlayer::sig_repaint,this,&OpenGLRender::OnRepaintRequested,Qt::QueuedConnection);
}

void AVPlayer::Close()
{
    //先让两条播放线程退出并回收，之后才能安全释放它们引用的 demuxer
    stop_ = true;
    if(audioThread_ && audioThread_->joinable())
    {
        audioThread_->join();
        audioThread_.reset();
        audioThread_ = nullptr;
    }
    if(videoThread_ && videoThread_->joinable())
    {
        videoThread_->join();
        videoThread_.reset();
        videoThread_ = nullptr;
    }
    if(avDEMuxer_)
    {
        avDEMuxer_.reset();
        avDEMuxer_ = nullptr;
    }
    //丢弃最新帧并递增会话代号，使已排队的刷新通知不会绘制到下一次会话
    ResetPresentation();
}

bool AVPlayer::Connect(QString ip, uint16_t port, QString code)
{
    TcpSocket tcp_socket;
    tcp_socket.Create();
    if(!tcp_socket.Connect(ip.toStdString(),port))
    {
        qDebug() << "连接信令服务器失败";
        return false;
    }
    qDebug() << "连接信令服务器成功";
    //生成一个信令连接器
    sig_conn_.reset(new SigConnection(loop_->GetTaskSchduler().get(),tcp_socket.GetSocket(),code,SigConnection::CONTROLLING));//控制端
    sig_conn_->SetStopStreamCallBack([this](){
        this->HandleStopStream();
    });
    sig_conn_->SetStartStreamCallBack([this](const QString& streamAddr){
        return this->HandleStartStream(streamAddr);
    });
    if(sig_conn_->Start() != 0)
    {
        qDebug() << "加入信令服务器失败";
        sig_conn_.reset();
        return false;
    }
    return true;
}

void AVPlayer::StopRemote()
{
    Close();
    if(sig_conn_)
    {
        sig_conn_->DisConnect();
        sig_conn_.reset();
    }
}

void AVPlayer::audioPlay()
{
    //音频播放，从这个音频帧队列来取pcm去播放
    AVFramePtr frame = nullptr;
    while(!stop_ && avDEMuxer_ && avContext_)
    {
        //判断音频播放大小
        if(!AvailableBytes() || avContext_->audio_queue_.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        //pop
        avContext_->audio_queue_.pop(frame);
        Write(frame);
    }
}

void AVPlayer::videoPlay()
{
    //视频播放：取最新一帧交给 presenter，由它决定是否需要投递一次刷新通知
    AVFramePtr frame = nullptr;
    while(!stop_ && avDEMuxer_ && avContext_)
    {
        //每秒汇总一次链路统计；未满一秒时立即返回，不逐帧刷屏
        VideoPullStats::ReportIfDue();
        if(avContext_->video_queue_.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        //pop：单槽队列里是当前最新的解码帧
        avContext_->video_queue_.pop(frame);
        VideoPullStats::OnFrameFetched();
        //只更新 latestFrame_；覆盖尚未绘制的旧帧时计入统计
        const VideoFramePresenter::PublishResult published = Presenter().Publish(frame);
        if(published.overwritten)
        {
            VideoPullStats::OnLatestFrameOverwritten();
        }
        if(!published.notify)
        {
            //已有未执行的刷新通知，Qt 的 update() 会合并绘制，无需再投递
            continue;
        }
        //发出时刻随通知进事件队列，槽里据此统计通知在事件队列的等待
        const qint64 emitUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        sig_repaint(published.sessionId,emitUs);
    }
}

void AVPlayer::resizeEvent(QResizeEvent *event)
{
    OpenGLRender::resizeEvent(event);
}

void AVPlayer::wheelEvent(QWheelEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        //准备一个事件
        Wheel_Body body;
        //获取这个滚轮值
        body.wheel = event->Wheel;
        QPoint pixeDelta= event->pixelDelta();
        QPoint angleDelta = event->angleDelta();
        body.wheel = (!pixeDelta.isNull() ? (pixeDelta.y() > 0 ? 1 : -1) : (angleDelta.y() > 0 ? 1 : -1));
        //发送这个数据包
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::wheelEvent(event);
}

void AVPlayer::mouseMoveEvent(QMouseEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        MouseMove_Body body;
        //获取这个x,y比值，后面实现
        this->GetPosRation(body);
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::mouseMoveEvent(event);
}

void AVPlayer::mousePressEvent(QMouseEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        Mouse_Body body;
        body.type = MouseKeyType::PRESS;
        body.mouseButtons = (MouseType)event->button();
        //发送
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::mousePressEvent(event);
}

void AVPlayer::mouseReleaseEvent(QMouseEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        Mouse_Body body;
        body.type = MouseKeyType::RELEASE;
        body.mouseButtons = (MouseType)event->button();
        //发送
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::mouseReleaseEvent(event);
}

void AVPlayer::keyPressEvent(QKeyEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        Key_Body body;
        body.type = MouseKeyType::PRESS;
        body.key = event->key();
        //发送
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::keyPressEvent(event);
}

void AVPlayer::keyReleaseEvent(QKeyEvent *event)
{
    if(!sig_conn_->IsClosed())
    {
        Key_Body body;
        body.key = MouseKeyType::RELEASE;
        body.key = event->key();
        //发送
        sig_conn_->Send((const char*)&body,body.len);
    }
    QWidget::keyReleaseEvent(event);
}

void AVPlayer::HandleStopStream()
{
    //停止拉流
    Close();
}

bool AVPlayer::HandleStartStream(const QString &streamAddr)
{
    //开始拉流
    // qInfo() << "[TRACE-PULL-20260814] start pull" << streamAddr;
    //停止/重连后 Close() 已释放 demuxer 并把 stop_ 置位，这里必须重建解封装器、
    //复位运行标志，否则重连时会空指针或新线程立刻退出
    if(!avDEMuxer_)
    {
        avDEMuxer_.reset(new AVDEMuxer(avContext_));
        avDEMuxer_->SetStreamCallBack([](bool){});
    }
    stop_ = false;
    //丢弃上一会话遗留的帧与待处理通知，新一轮拉流从零开始统计
    avContext_->video_queue_.clear();
    avContext_->audio_queue_.clear();
    ResetPresentation();
    VideoPullStats::Reset();
    if(!avDEMuxer_->Open(streamAddr.toStdString()))
    {
        qWarning() << "failed to start demux thread";
        return false;
    }

    //开始启动线程
    audioThread_.reset(new std::thread([this](){
        this->audioPlay();
    }));
    videoThread_.reset(new std::thread([this](){
        this->videoPlay();
    }));
    return true;
}
