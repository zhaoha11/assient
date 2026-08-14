#ifndef _ORMMANAGER_H_
#define _ORMMANAGER_H_
#include <memory>
#include <mutex>
#include <string>
#include <mysql/mysql.h>

enum class AuthenticateResult
{
    Success,
    AccountNotFound,
    PasswordIncorrect,
    DatabaseError
};

class ORMManager
{
public:
    ~ORMManager();
    // 获取 ORMManager 的全局单例。
    static ORMManager *GetInstance();
    void UserRegister(const char *name, const char *acount, const char *password, const char *usercode, const char *sig_server);
    MYSQL_ROW UserLogin(const char *usercode);
    AuthenticateResult Authenticate(const std::string &account, const std::string &password,
                                    std::string &usercode, bool &online);
    //MYSQL_ROW UserLogin(const char *acc,const char* pas);
    void UserDestroy(const char *usercode);
    void UpdateClientOnline(const char *usercode, int online, long recently_login, const char *sig_server);
    void insertClient(const char *name, const char *acount, const char *password, const char *usercode, int online, long recently_login, const char *sig_server);

protected:
    void deleteClientByUsercode(const char *usercode);
    MYSQL_ROW selectClientByUsercode(const char *usercode);

private:
    ORMManager();
    MYSQL mysql_;

private:
    static std::unique_ptr<ORMManager> instance_;
};
#endif