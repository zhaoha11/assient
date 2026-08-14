#include "LoginConnection.h"
#include "ORMManager.h"
#include <chrono>
#define TIMEOUT 60

LoginConnection::LoginConnection(TaskScheduler *scheduler, int socket)
    : TcpConnection(scheduler, socket)
{
    // 设置连接读回调，收到客户端数据后交给 OnRead 处理。
    this->SetReadCallback([this](std::shared_ptr<TcpConnection> conn, BufferReader &buffer)
                          { return this->OnRead(buffer); });
}

LoginConnection::~LoginConnection()
{
    Clear();
}

bool LoginConnection::OnRead(BufferReader &buffer)
{

    if (buffer.ReadableBytes() > 0)
    {
        HandleMessage(buffer);
    }
    return true;
}
// 从接收缓冲区解析并处理一个完整的数据包。
void LoginConnection::HandleMessage(BufferReader &buffer)
{
    if (buffer.ReadableBytes() < sizeof(packet_head))
    {
        return;
    }
    packet_head *head = (packet_head *)buffer.Peek();
    if (buffer.ReadableBytes() < head->len)
    {
        return;
    }
    switch (head->cmd)
    {
    case Login:
        HandleLogin((packet_head *)buffer.Peek());
        break;
    case Register:
        HandleRegister((packet_head *)buffer.Peek());
        break;
    case Destory:
        HandleDestory((packet_head *)buffer.Peek());
        break;
    default:
        break;
    }
    // 移除已经处理的数据，保留尚未接收完整的后续数据。
    buffer.Retrieve(head->len);
}

bool LoginConnection::IsTimeout(uint64_t timestamp) // 返回 true 表示请求已经超时。
{
    // 获取当前 Unix 时间戳，单位为秒。
    auto now = std::chrono::system_clock::now();
    auto nowTimestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // 客户端与服务端时间差超过允许范围时，认为请求无效。
    int64_t time = nowTimestamp - timestamp;
    return abs(time) > TIMEOUT;
}

void LoginConnection::Clear()
{
    if (logged_in_ && !user_code_.empty())
    {
        ORMManager::GetInstance()->UpdateClientOnline(user_code_.c_str(), 0, 0, "");
        logged_in_ = false;
        user_code_.clear();
    }
}

void LoginConnection::HandleRegister(const packet_head *data)
{
    RegisterResult reply;
    // 注册前先判断该设备 code 是否已经存在。
    UserRegister *login = (UserRegister *)data;
    uint64_t time = login->timestamp;
    if (IsTimeout(time))
    {
        reply.resultCode = REQUEST_TIMEOUT;
    }
    else
    {
        std::string code = login->GetCode();
       // std::string account = login->GetCount();
        //std::string password = login->GetPasswd();
        // 根据设备 code 查询数据库中的用户记录。
        MYSQL_ROW row = ORMManager::GetInstance()->UserLogin(code.c_str());
        if (row == NULL) // 尚未注册。
        {
            ORMManager::GetInstance()->UserRegister(login->GetName().c_str(), login->GetCount().c_str(), login->GetPasswd().c_str(), login->GetCode().c_str(), "172.21.205.121");
            reply.resultCode = S_OK;
        }
        else
        {
            reply.resultCode = ALREADY_REDISTERED;
        }
    }
    this->Send((const char *)&reply, reply.len);
}

void LoginConnection::HandleLogin(const packet_head *data)
{
    LoginResult reply;
    const UserLogin *login = (const UserLogin *)data;
    if (IsTimeout(login->timestamp))
    {
        reply.resultCode = REQUEST_TIMEOUT;
    }
    else
    {
        const std::string account(login->count.data(),
                                  strnlen(login->count.data(), login->count.size()));
        const std::string password(login->passwd.data(),
                                   strnlen(login->passwd.data(), login->passwd.size()));
        std::string usercode;
        bool online = false;

        const AuthenticateResult authResult =
            ORMManager::GetInstance()->Authenticate(account, password, usercode, online);
        if (authResult == AuthenticateResult::AccountNotFound)
        {
            reply.resultCode = ACCOUNT_NOT_FOUND;
        }
        else if (authResult == AuthenticateResult::PasswordIncorrect)
        {
            reply.resultCode = PASSWORD_INCORRECT;
        }
        else if (authResult != AuthenticateResult::Success)
        {
            reply.resultCode = VERFICATE_FAILED;
        }
        else if (online)
        {
            reply.resultCode = ALREADY_LOGIN;
        }
        else
        {
            user_code_ = usercode;
            logged_in_ = true;
            reply.resultCode = S_OK;
            reply.SetIp("192.168.3.130");
            reply.port = 6539;
            // 信令身份以数据库 USER_CODE 为准，不使用登录包中的临时 code。
            reply.SetCode(usercode);

            const auto now = std::chrono::system_clock::now();
            const auto nowTimestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            ORMManager::GetInstance()->UpdateClientOnline(user_code_.c_str(),
                                                           1,
                                                           nowTimestamp,
                                                           "192.168.3.130");
        }
    }

    printf("[LoginSvr] send LoginResult: result=%d, sigIp=%s, sigPort=%u, userCode=%s\n",
           static_cast<int>(reply.resultCode), reply.GetIp().c_str(),
           static_cast<unsigned int>(reply.port), reply.GetCode().c_str());
    this->Send((const char *)&reply, reply.len);
}
void LoginConnection::HandleDestory(const packet_head *data)
{
    // 注销用户并删除对应的数据库记录。
    UserDestory *destory = (UserDestory *)data;
    ORMManager::GetInstance()->UserDestroy(destory->GetCode().c_str());
}
