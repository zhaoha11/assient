#ifndef _DEFINE_H_
#define _DEFINE_H_
#include <cstdint>
#include <string>
#include <array>
#include <sys/sysinfo.h>
#include <string.h>

#pragma pack(push, 1)
enum Cmd : uint16_t
{
    Minotor,
    ERROR,
    Login,
    Register,
    Destory,
};

enum ResultCode
{
    S_OK = 0,
    SERVER_ERROR,
    REQUEST_TIMEOUT,
    ALREADY_REDISTERED,
    USER_DISAPPEAR,
    ALREADY_LOGIN,
    VERFICATE_FAILED,
    ACCOUNT_NOT_FOUND, // 错误码数值 7
    PASSWORD_INCORRECT // 数值 8
};

struct packet_head
{
    packet_head()
        : len(-1), cmd(-1) {}
    uint16_t len;
    uint16_t cmd;
};

struct UserRegister : public packet_head
{
    UserRegister() : packet_head()
    {
        cmd = Register;
        len = sizeof(UserRegister);
    }
    void SetCode(const std::string &str)
    {
        str.copy(code.data(), code.size(), 0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    void SetName(const std::string &str)
    {
        str.copy(name.data(), name.size(), 0);
    }
    std::string GetName()
    {
        return std::string(name.data());
    }
    void SetCount(const std::string &str)
    {
        str.copy(count.data(), count.size(), 0);
    }
    std::string GetCount()
    {
        return std::string(count.data());
    }
    void SetPasswd(const std::string &str)
    {
        str.copy(passwd.data(), passwd.size(), 0);
    }
    std::string GetPasswd()
    {
        return std::string(passwd.data());
    }
    std::array<char, 20> code;
    std::array<char, 20> name;
    std::array<char, 12> count;
    std::array<char, 20> passwd;
    uint64_t timestamp;
};

struct UserLogin : public packet_head
{
    UserLogin() : packet_head()
    {
        cmd = Login;
        len = sizeof(UserLogin);
    }
    void SetCode(const std::string &str)
    {
        str.copy(code.data(), code.size(), 0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    void SetCount(const std::string &str)
    {
        str.copy(count.data(), count.size(), 0);
    }
    std::string GetCount()
    {
        return std::string(count.data());
    }
    void SetPasswd(const std::string &str)
    {
        str.copy(passwd.data(), passwd.size(), 0);
    }
    std::string GetPasswd()
    {
        return std::string(passwd.data());
    }
    std::array<char, 20> code;
    std::array<char, 12> count;
    std::array<char, 33> passwd; // Md5
    uint64_t timestamp;
};

struct RegisterResult : public packet_head
{
    RegisterResult() : packet_head()
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
    void SetIp(const std::string &str)
    {
        ctrSvrIp.fill('\0');
        strncpy(ctrSvrIp.data(), str.c_str(), ctrSvrIp.size() - 1);
        ctrSvrIp.back() = '\0';
    }
    std::string GetIp()
    {
        return std::string(ctrSvrIp.data());
    }
    void SetCode(const std::string &str)
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
    // 登录成功后返回数据库中的 USER_CODE，供客户端注册信令身份。
    std::array<char, 20> code;
};

struct UserDestory : public packet_head
{
    UserDestory() : packet_head()
    {
        cmd = Destory;
        len = sizeof(UserDestory);
    }
    void SeCode(const std::string &str)
    {
        str.copy(code.data(), code.size(), 0);
    }
    std::string GetCode()
    {
        return std::string(code.data());
    }
    std::array<char, 20> code;
};

struct Monitor_body : public packet_head
{
    Monitor_body()
        : packet_head()
    {
        cmd = Minotor;
        len = sizeof(Monitor_body);
        ip.fill('\0');
    }
    void SetIp(const std::string &str)
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
#pragma pack(pop)
#endif