#include "SigConnection.h"
#include "defin.h"
#include <QDebug>
#include <QUuid>
#include <windows.h>
#include <QGuiApplication>

int streamIndex = 1;
namespace
{
constexpr int kMaxControlJoinRetries = 3;
constexpr int kSignalCodeMaxLength = 9;
}

SigConnection::SigConnection(TaskScheduler *scheduler, int sockfd, const QString& code,const UserType &type)
    :TcpConnection(scheduler,sockfd)
    ,state_(NONE)
    ,type_(type)
{
    if (type_ == CONTROLLED)
    {
        // Controlled connections register with the database USER_CODE.
        joinCode_ = code;
    }
    else
    {
        // Controllers keep the target code and register a session-specific code.
        targetCode_ = code;
        joinCode_ = GenerateControlSessionCode();
    }

    //设置回调函数
    SetReadCallback([this](std::shared_ptr<TcpConnection> conn,BufferReader& buffer){
        return this->OnRead(buffer);
    });
    //关闭回调
    SetCloseCallback([this](std::shared_ptr<TcpConnection> conn){
        return this->OnClose();
    });

    screen_ = QGuiApplication::primaryScreen();
}
QString SigConnection::GenerateControlSessionCode()
{
    // Join_body.id stores at most 9 bytes: C plus 8 random hex characters.
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    uuid.remove('-');
    return QStringLiteral("C") + uuid.left(8).toUpper();
}

SigConnection::~SigConnection()
{

}

qint32 SigConnection::Start()
{
    //连接之后需要去创建房间
    return Join();
}

bool SigConnection::OnRead(BufferReader &buffer)
{
    while(buffer.ReadableBytes() >= sizeof(packet_head))
    {
        packet_head* head = (packet_head*)buffer.Peek();
        if(buffer.ReadableBytes() < head->len)
        {
            break;
        }
        HandleMessage(buffer);
    }
    return true;
}

void SigConnection::OnClose()
{
    quit_ = true;
}

void SigConnection::HandleMessage(BufferReader &buffer)
{
    //处理message
    if(buffer.ReadableBytes() < sizeof(packet_head))
    {
        return ;
    }
    packet_head* head = (packet_head*)buffer.Peek();
    if(buffer.ReadableBytes() < head->len)
    {
        return ; //要去返回true,不能返回false
    }
    //数据完整
    switch (head->cmd) {
    case JOIN:
        doJoin(head); //处理创建房间
        break;
    case OBTAINSTREAM:
        doObtainStreamReply(head);
        break;
    case PLAYSTREAM:
        doPlayStream(head);
        break;
    case CREATESTREAM:
        doCtreatStream(head);
        break;
    case DELETESTREAM:
        doDeleteStream(head);
        break;
    case MOUSE:
        doMouseEvent(head);
        break;
    case MOUSEMOVE:
        doMouseMoveEvent(head);
        break;
    case KEY:
        doKeyEvent(head);
        break;
    case WHEEL:
        doWheelEvent(head);
        break;
    default:
        break;
    }
    //更新bufferr缓冲区
    buffer.Retrieve(head->len);
}

qint32 SigConnection::Join()
{
    //创建房间
    if(state_ != NONE)//当前已经创建房间
    {
        return -1;
    }
    //申请创建房间
    if (joinCode_.isEmpty() || joinCode_.toUtf8().size() > kSignalCodeMaxLength)
    {
        qWarning() << "[Sig] invalid join code:" << joinCode_;
        return -1;
    }

    Join_body body;
    body.SetId(joinCode_.toStdString());
    qDebug() << "[Sig] send JOIN, role =" << (type_ == CONTROLLED ? "controlled" : "controlling") << "joinCode =" << joinCode_;
    this->Send((const char*)&body,body.len);
    return 0;
}

qint32 SigConnection::obtainStream()
{
    //获取流，由控制端主动发送
    if(state_ == IDLE && type_ == CONTROLLING)
    {
        //获取流
        if (targetCode_.isEmpty() || targetCode_.toUtf8().size() > kSignalCodeMaxLength)
        {
            qWarning() << "[Sig] invalid target code:" << targetCode_;
            return -1;
        }
        ObtainStream_body body;
        body.SetId(targetCode_.toStdString());
        this->Send((const char*)&body,body.len);
        return 0;
    }
    return -1;
}

void SigConnection::doJoin(const packet_head* data)
{
    //处理创建房间
    JoinReply_body* reply = (JoinReply_body*)data;
    if(reply->result == S_OK)
    {
        //更新状态
        state_ = IDLE;
        if(type_ == CONTROLLING)
        {
            //控制端开始申请流
            if(obtainStream() != 0)
            {
                qDebug() << "获取流请求发送失败";
            }
            else
            {
                state_ = PULLER;
                qDebug() << "获取流请求发送成功";
            }
        }
        return;
    }

    if (type_ == CONTROLLING && joinRetryCount_ < kMaxControlJoinRetries)
    {
        ++joinRetryCount_;
        joinCode_ = GenerateControlSessionCode();
        qWarning() << "[Sig] JOIN collision, retry" << joinRetryCount_ << "with joinCode =" << joinCode_;
        Join();
        return;
    }

    qWarning() << "[Sig] JOIN failed, joinCode =" << joinCode_;
    return;

}
void SigConnection::doObtainStreamReply(const packet_head* data)
{
    ObtainStreamReply_body* reply = (ObtainStreamReply_body*)data;
    if (reply->result == S_OK)
    {
        qDebug() << "[Sig] obtain stream accepted, targetCode =" << targetCode_;
        return;
    }

    // A rejected request must not leave this controller in the PULLER state.
    state_ = IDLE;
    qWarning() << "[Sig] obtain stream rejected: target is offline, invalid, or already controlled, targetCode =" << targetCode_;
}

void SigConnection::doPlayStream(const packet_head* data)
{
    //播放流，控制端来去播放
    if(state_ == PULLER && type_ == CONTROLLING)
    {
        //开始播放流
        PlayStream_body* playStream = (PlayStream_body*)data;
        if(playStream->result == S_OK)
        {
            //需要获取这个流地址
            //通过这个回调函数返回
            qDebug() << "开始播放流";
            if(startStreamCb_)
            {
                startStreamCb_(QString::fromStdString(playStream->GetstreamAddres()));
            }
        }
        else
        {
            qDebug() << "播放流失败";
        }
    }

}

void SigConnection::doCtreatStream(const packet_head* data)
{
    //创建流，由被控端来处理
    if(state_ == IDLE && type_ == CONTROLLED)
    {
        CreateStreamReply_body reply;
        //准备一个流地址
        QString streamAddr = "rtmp://192.168.3.130:1935/live/" + QString::number(++streamIndex);
        //开始推流
        if(startStreamCb_)
        {
            //传到外部，由这个推流器开始推流 ,是否推流成功
            if(startStreamCb_(streamAddr))
            {
                //推流成功
                reply.SetstreamAddres(streamAddr.toStdString());
                reply.SetCode((ResultCode)0);
                //发送
                qDebug() << "streamaddr: " << reply.GetstreamAddres().c_str() << "len: " << reply.len;
                this->Send((const char*)&reply,reply.len);
                state_ = PUSHER;
            }
            else
            {
                //推流失败
                qDebug() << "streamaddr failed: ";
                reply.SetCode(SERVER_ERROR);
                this->Send((const char*)&reply,reply.len);
            }
        }
    }
}

void SigConnection::doDeleteStream(const packet_head* data)
{
    //删除流 如果推流端发现这个拉流数量为0,我们就需要停止推流，如果有一个或者多个拉流端就需要继续推流‘
    DeleteStream_body* body = (DeleteStream_body*)data;
    if(body->streamCount == 0)
    {
        //停止推流
        if(stopStreamCb_)
        {
            stopStreamCb_();
        }
    }
}

void SigConnection::doMouseEvent(const packet_head *data)
{
    //处理鼠标按下松开事件
    Mouse_Body* body = (Mouse_Body*)data;
    DWORD dwFlags = 0;
    if(body->type == PRESS)
    {
        dwFlags |= (body->mouseButtons & MouseType::LeftButton) ? MOUSEEVENTF_LEFTDOWN : 0;
        dwFlags |= (body->mouseButtons & MouseType::RightButton) ? MOUSEEVENTF_RIGHTDOWN : 0;
        dwFlags |= (body->mouseButtons & MouseType::MiddleButton) ? MOUSEEVENTF_MIDDLEDOWN : 0;
    }
    else if(body->type == RELEASE)
    {
        dwFlags |= (body->mouseButtons & MouseType::LeftButton) ? MOUSEEVENTF_LEFTUP : 0;
        dwFlags |= (body->mouseButtons & MouseType::RightButton) ? MOUSEEVENTF_RIGHTUP : 0;
        dwFlags |= (body->mouseButtons & MouseType::MiddleButton) ? MOUSEEVENTF_MIDDLEUP : 0;
    }
    //模拟事件
    if(dwFlags != 0)
    {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = dwFlags;
        SendInput(1,&input,sizeof(input));
    }
}

void SigConnection::doMouseMoveEvent(const packet_head *data)
{
    //鼠标移动
    //传来的时候他是一个double 左边和右边，我们需要去组成浮点数，这个值他是一个比值(x/w,y/h),组成一个浮点数，乘上当前这个电脑高/宽，来获取当前的坐标
    MouseMove_Body* body = (MouseMove_Body*)data;
    //获取当前电脑宽高
    double combined_x = (static_cast<double>(body->xl_ratio) + (static_cast<double>(body->xr_ratio) / 100.0f)) / 100.0f;
    double combined_y = (static_cast<double>(body->yl_ratio) + (static_cast<double>(body->yr_ratio) / 100.0f)) / 100.0f;
    int x = static_cast<int>(combined_x * screen_->size().width() / screen_->devicePixelRatio());
    int y = static_cast<int>(combined_y * screen_->size().height() / screen_->devicePixelRatio());
    QCursor::setPos(x,y);
}

void SigConnection::doKeyEvent(const packet_head *data)
{
    //获取键值去模拟
    Key_Body* body = (Key_Body*)data;
    DWORD vk = body->key;
    qDebug() << "key: " << vk;
    INPUT input[1];
    ZeroMemory(input,sizeof(input));
    //操作类型是按下，松开
    int op = body->type ? KEYEVENTF_KEYUP : 0;
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = vk;
    input[0].ki.dwFlags = op;
    input[0].ki.wScan = MapVirtualKey(vk,0);
    input[0].ki.time = 0;
    input[0].ki.dwExtraInfo = 0;
    SendInput(1,input,sizeof(input));
}

void SigConnection::doWheelEvent(const packet_head *data)
{
    Wheel_Body* body = (Wheel_Body*)data;
    //获取滚轮是向上还是向下滚动
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = body->wheel * 240;
    SendInput(1,&input,sizeof(input));
}
