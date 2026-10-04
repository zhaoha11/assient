#ifndef AVPLAYER_H
#define AVPLAYER_H
#include "OpenGLRender.h"
#include "AudioRender.h"
#include "AVDEMuxer.h"
#include <atomic>

class EventLoop;
class SigConnection;
class AVPlayer : public OpenGLRender ,public AudioRender
{
    Q_OBJECT
public:
    ~AVPlayer();
    explicit AVPlayer(EventLoop* loop,QWidget* parent = nullptr);
    bool Connect(QString ip,uint16_t port,QString code);
    void StopRemote();
signals:
    //刷新通知不携带帧，只带会话代号和发出时刻（steady_clock 微秒）：
    //会话代号让旧会话遗留的排队通知作废，时间戳随通知走事件队列，
    //用于统计通知在 Qt 事件队列里的等待（共享时间戳在积压时测不出来）
    void sig_repaint(quint64 sessionId,qint64 emitUs);
protected:
    void audioPlay();
    void videoPlay();
    void Init();
    void Close();
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual void wheelEvent(QWheelEvent *event) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void mouseReleaseEvent(QMouseEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *event) override;
    virtual void keyReleaseEvent(QKeyEvent *event) override;
private:
    void HandleStopStream();
    bool HandleStartStream(const QString& streamAddr);
private:
    //播放线程会读取，停止时由 GUI 线程写入，必须是原子量
    std::atomic<bool> stop_{false};
    EventLoop* loop_;
    AVContext* avContext_ = nullptr;
    std::shared_ptr<SigConnection> sig_conn_;
    std::unique_ptr<AVDEMuxer> avDEMuxer_ = nullptr;
    std::unique_ptr<std::thread> audioThread_ = nullptr;
    std::unique_ptr<std::thread> videoThread_ = nullptr;
};
#endif // AVPLAYER_H
