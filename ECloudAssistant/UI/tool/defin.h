#ifndef DEFIN_H
#define DEFIN_H
#include <cstdint>
#include <string>
#include <array>
#include <cstring>

#pragma pack(push,1)
enum Cmd : uint16_t
{
    Minotor,
    ERROR_,
    Login,
    Register,
    Destory,
    JOIN,
    OBTAINSTREAM,
    CREATESTREAM,
    PLAYSTREAM,
    DELETESTREAM,
    MOUSE,
    MOUSEMOVE,
    KEY,
    WHEEL,
};

enum ResultCode
{
    S_OK_ = 0,
    SERVER_ERROR ,
    REQUEST_TIMEOUT ,
    ALREADY_REDISTERED ,
    USER_DISAPPEAR,
    ALREADY_LOGIN,
    VERFICATE_FAILED,
    ACCOUNT_NOT_FOUND,
    PASSWORD_INCORRECT,
};

struct packet_head {
    packet_head()
        :len(-1)
        , cmd(-1) {}
    uint16_t len;
    uint16_t cmd;
};

struct Login_Info : public packet_head
{
    Login_Info():packet_head()
    {
        cmd = Login;
        len = sizeof(Login_Info);
        timestamp = -1;
    }
    uint64_t timestamp;
};

//登录应答
struct LoginReply : public packet_head
{
    LoginReply():packet_head()
    {
        cmd = Login;  //如果请求超时，将这个cmd置为ERROR
        len = sizeof(LoginReply);
        port = -1;
        ip.fill('\0');
    }
    uint16_t port;
    std::array<char,16> ip;
};

struct UserRegister : public packet_head
{
    UserRegister():packet_head()
    {
        cmd = Register;
        len = sizeof(UserRegister);
    }
    void SetCode(const std::string& str)
    {
        str.copy(code.data(),code.size(),0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    void SetName(const std::string& str)
    {
        str.copy(name.data(),name.size(),0);
    }
    std::string GetName()
    {
        return std::string(name.data());
    }
    void SetCount(const std::string& str)
    {
        str.copy(count.data(),count.size(),0);
    }
    std::string GetCount()
    {
        return std::string(count.data());
    }
    void SetPasswd(const std::string& str)
    {
        str.copy(passwd.data(),passwd.size(),0);
    }
    std::string GetPasswd()
    {
        return std::string(passwd.data());
    }
    std::array<char,20> code;
    std::array<char,20> name;
    std::array<char,12> count;
    std::array<char,20> passwd;
    uint64_t timestamp;
};

struct UserLogin : public packet_head
{
    UserLogin():packet_head()
    {
        cmd = Login;
        len = sizeof(UserLogin);
        code.fill('\0');
        count.fill('\0');
        passwd.fill('\0');
    }
    void SetCode(const std::string& str)
    {
        code.fill('\0');
        str.copy(code.data(), code.size() - 1, 0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    void SetCount(const std::string& str)
    {
        count.fill('\0');
        str.copy(count.data(), count.size() - 1, 0);
    }
    std::string GetCount()
    {
        return std::string(count.data());
    }
    void SetPasswd(const std::string& str)
    {
        passwd.fill('\0');
        str.copy(passwd.data(), passwd.size() - 1, 0);
    }
    std::string GetPasswd()
    {
        return std::string(passwd.data());
    }
    std::array<char,20> code;
    std::array<char,12> count;
    std::array<char,33> passwd;
    uint64_t timestamp;
};

struct RegisterResult : public packet_head
{
    RegisterResult():packet_head()
    {
        cmd = Register;
        len = sizeof(RegisterResult);
    }
    ResultCode resultCode;
};

struct LoginResult : public packet_head
{
    LoginResult() : packet_head()
    {
        cmd = Login;
        len = sizeof(LoginResult);
        resultCode = SERVER_ERROR;
        port = 0;
        ctrSvrIp.fill('\0');
        code.fill('\0');
    }
    void SetIp(const std::string& str)
    {
        ctrSvrIp.fill('\0');
        strncpy(ctrSvrIp.data(), str.c_str(), ctrSvrIp.size() - 1);
        ctrSvrIp.back() = '\0';
    }
    std::string GetIp()
    {
        return std::string(ctrSvrIp.data());
    }
    void SetCode(const std::string& str)
    {
        code.fill('\0');
        str.copy(code.data(), code.size() - 1, 0);
    }
    std::string GetCode() const
    {
        return std::string(code.data());
    }
    ResultCode resultCode;
    uint16_t port;
    std::array<char, 16> ctrSvrIp;
    // 登录成功后由服务端返回，用作客户端在信令服务器中的唯一标识。
    std::array<char, 20> code;
};

struct UserDestory : public packet_head
{
    UserDestory(): packet_head()
    {
        cmd = Destory;
        len = sizeof(UserDestory);
    }
    void SeCode(const std::string& str)
    {
        str.copy(code.data(),code.size(),0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    std::array<char,20> code;
};

struct Monitor_body : public packet_head {
    Monitor_body()
        :packet_head()
    {
        cmd = Minotor;
        len = sizeof(Monitor_body);
        ip.fill('\0');
    }
    void SetIp(const std::string& str)
    {
        ip.fill('\0');
        str.copy(ip.data(), ip.size() - 1, 0);
    }
    std::string GetIp()
    {
        return std::string(ip.data());
    }
    uint8_t mem;
    std::array<char, 16> ip;
    uint16_t port;
};

//创建房间
struct Join_body : public packet_head
{
    Join_body() : packet_head()
    {
        cmd = JOIN;
        len = sizeof(Join_body);
        id.fill('\0');
    }
    void SetId(const std::string& str)
    {
        str.copy(id.data(),id.size(),0);
    }
    std::string GetId()
    {
        return std::string(id.data());
    }
    std::array<char,10> id;
};
//创建房间应答
struct JoinReply_body : public packet_head
{
    JoinReply_body() : packet_head()
    {
        cmd = JOIN;
        len = sizeof(JoinReply_body);
        result = SERVER_ERROR;
    }
    //设置结果
    void SetCode(const ResultCode code)
    {
        result = code;
    }
    ResultCode result;
};
//获取流
struct ObtainStream_body : public packet_head
{
    ObtainStream_body() : packet_head()
    {
        cmd = OBTAINSTREAM;
        len = sizeof(ObtainStream_body);
        id.fill('\0');
    }
    void SetId(const std::string& str)
    {
        str.copy(id.data(),id.size(),0);
    }
    std::string GetId()
    {
        return std::string(id.data());
    }
    std::array<char,10> id;
};
//获取流应答
struct ObtainStreamReply_body : public packet_head
{
    ObtainStreamReply_body() : packet_head()
    {
        cmd = OBTAINSTREAM;
        len = sizeof(ObtainStreamReply_body);
        result = SERVER_ERROR;
    }
    void SetCode(const ResultCode code)
    {
        result = code;
    }
    ResultCode result;
};
//创建流
struct CreateStream_body : public packet_head
{
    CreateStream_body() : packet_head()
    {
        cmd = CREATESTREAM;
        len = sizeof(CreateStream_body);
    }
};
//创建流应答 流地址和结果
struct CreateStreamReply_body : public packet_head
{
    CreateStreamReply_body() : packet_head()
    {
        cmd = CREATESTREAM;
        len = sizeof(CreateStreamReply_body);
        result = SERVER_ERROR;
        streamAddres.fill('\0');
    }
    void SetstreamAddres(const std::string& str)
    {
        str.copy(streamAddres.data(),streamAddres.size(),0);
    }
    std::string GetstreamAddres()
    {
        return std::string(streamAddres.data());
    }
    void SetCode(const ResultCode code)
    {
        result = code;
    }
    ResultCode result;
    std::array<char,70> streamAddres;
};

//播放流 提供播放流地址
struct PlayStream_body : public packet_head
{
    PlayStream_body() : packet_head()
    {
        cmd = PLAYSTREAM;
        len = sizeof(PlayStream_body);
        result = SERVER_ERROR;
        streamAddres.fill('\0');
    }
    void SetstreamAddres(const std::string& str)
    {
        str.copy(streamAddres.data(),streamAddres.size(),0);
    }
    std::string GetstreamAddres()
    {
        return std::string(streamAddres.data());
    }
    void SetCode(const ResultCode code)
    {
        result = code;
    }
    ResultCode result;
    std::array<char,70> streamAddres;
};

struct PlayStreamReplay_body : public packet_head
{
    PlayStreamReplay_body() : packet_head()
    {
        cmd = PLAYSTREAM;
        len = sizeof(PlayStreamReplay_body);
        result = SERVER_ERROR;
    }
    void SetCode(const ResultCode code)
    {
        result = code;
    }
    ResultCode result;
};

struct DeleteStream_body : public packet_head
{
    DeleteStream_body():packet_head()
    {
        cmd = DELETESTREAM;
        streamCount = -1;
        len = sizeof(DeleteStream_body);
    }
    void SetStreamCount(const int count)
    {
        streamCount = count;
    }
    int streamCount;//推流的时候，如果发现拉流数量为0,我们就需要停止推流，如果流数量不为0，就说明还有客户端连接，不能停止推流
};

enum MouseType : uint8_t
{
    NoButton         = 0,
    LeftButton       = 1,
    RightButton      = 2,
    MiddleButton     = 4,
    XButton1         = 8,
    XButton2         = 16,
};

enum MouseKeyType : uint8_t
{
    PRESS,
    RELEASE
};

struct Key_Body : public packet_head
{
    Key_Body()
        :packet_head()
    {
        cmd = KEY;
        len = sizeof(Key_Body);
    }
    MouseKeyType type;
    uint16_t  key;
};

struct Wheel_Body : public packet_head
{
    Wheel_Body()
        :packet_head()
    {
        cmd = WHEEL;
        len = sizeof(Wheel_Body);
    }
    int8_t wheel;
};

struct MouseMove_Body : public packet_head
{
    MouseMove_Body()
        :packet_head()
    {
        cmd = MOUSEMOVE;
        len = sizeof(MouseMove_Body);
    }
    uint8_t xl_ratio;
    uint8_t xr_ratio;
    uint8_t yl_ratio;
    uint8_t yr_ratio;
};

struct Mouse_Body : public packet_head
{
    Mouse_Body()
        :packet_head()
    {
        cmd = MOUSE;
        len = sizeof(Mouse_Body);
    }
    MouseKeyType type;
    MouseType mouseButtons;
};

#pragma pack(pop)
#endif // DEFIN_H
