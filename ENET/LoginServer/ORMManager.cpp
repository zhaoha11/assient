#include "ORMManager.h"
#include <chrono>
#include <iostream>

std::unique_ptr<ORMManager> ORMManager::instance_ = nullptr;

ORMManager::ORMManager()
{
    mysql_init(&mysql_);
    if (mysql_real_connect(&mysql_, "127.0.0.1", "enet", "enet123456", "users", 3306, NULL, 0) == NULL)
    {
        printf("Connect MySql failed: %s\n", mysql_error(&mysql_));
        return;
    }
    printf("Connect MySql successful\n");
}

ORMManager::~ORMManager()
{
    mysql_close(&mysql_);
}

ORMManager *ORMManager::GetInstance()
{
    static std::once_flag flag;
    std::call_once(flag, [&]()
                   {
                       instance_.reset(new ORMManager());
                   });
    return instance_.get();
}

void ORMManager::UserRegister(const char *name, const char *acount, const char *password, const char *usercode, const char *sig_server)
{
    auto now = std::chrono::system_clock::now();
    auto nowTimestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return insertClient(name, acount, password, usercode, 0, nowTimestamp, sig_server);
}

MYSQL_ROW ORMManager::UserLogin(const char *usercode)
{
    return selectClientByUsercode(usercode);
}
AuthenticateResult ORMManager::Authenticate(const std::string &account, const std::string &password,
                                            std::string &usercode, bool &online)
{
    usercode.clear();
    online = false;
    if (account.empty() || password.empty())
    {
        printf("[LoginSvr][Auth] failed: empty account or password, account='%s', password='%s'\n",
               account.c_str(), password.c_str());
        return account.empty() ? AuthenticateResult::AccountNotFound : AuthenticateResult::PasswordIncorrect;
    }

    char escaped_account[2 * 12 + 1] = {0};
    mysql_real_escape_string(&mysql_, escaped_account, account.c_str(), account.size());

    char query[256] = {0};
    snprintf(query, sizeof(query),
             "SELECT USER_CODE, USER_PASSWD, USER_ONLINE "
             "FROM clients WHERE USER_ACOUNT = '%s' LIMIT 1",
             escaped_account);
    if (mysql_query(&mysql_, query))
    {
        printf("[LoginSvr][Auth] query failed: account='%s', password='%s', mysqlError='%s'\n",
               account.c_str(), password.c_str(), mysql_error(&mysql_));
        return AuthenticateResult::DatabaseError;
    }

    MYSQL_RES *result = mysql_store_result(&mysql_);
    if (result == NULL)
    {
        printf("[LoginSvr][Auth] store result failed: account='%s', password='%s', mysqlError='%s'\n",
               account.c_str(), password.c_str(), mysql_error(&mysql_));
        return AuthenticateResult::DatabaseError;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == NULL)
    {
        printf("[LoginSvr][Auth] failed: account not found, account='%s', password='%s'\n",
               account.c_str(), password.c_str());
        mysql_free_result(result);
        return AuthenticateResult::AccountNotFound;
    }

    if (row[1] == NULL)
    {
        printf("[LoginSvr][Auth] failed: database password is NULL, account='%s', password='%s'\n",
               account.c_str(), password.c_str());
        mysql_free_result(result);
        return AuthenticateResult::PasswordIncorrect;
    }

    if (password != row[1])
    {
        printf("[LoginSvr][Auth] failed: password mismatch, account='%s', clientPassword='%s', databasePassword='%s'\n",
               account.c_str(), password.c_str(), row[1]);
        mysql_free_result(result);
        return AuthenticateResult::PasswordIncorrect;
    }

    if (row[0] == NULL || row[2] == NULL)
    {
        printf("[LoginSvr][Auth] failed: invalid database row, account='%s', password='%s'\n",
               account.c_str(), password.c_str());
        mysql_free_result(result);
        return AuthenticateResult::DatabaseError;
    }

    usercode = row[0];
    online = atoi(row[2]) != 0;
    mysql_free_result(result);
    return AuthenticateResult::Success;
}

void ORMManager::UserDestroy(const char *usercode)
{
    return deleteClientByUsercode(usercode);
}

void ORMManager::UpdateClientOnline(const char *usercode, int online, long recently_login, const char *sig_server)
{
    char query[1024];

    snprintf(
        query,
        sizeof(query),
        "UPDATE clients "
        "SET USER_ONLINE = %d, "
        "USER_RECENTLY_LOGIN = %ld, "
        "USER_SVR_MOUNT = '%s' "
        "WHERE USER_CODE = '%s'",
        online,
        recently_login,
        sig_server,
        usercode);

    if (mysql_query(&mysql_, query))
    {
        printf("Update online state failed: %s\n", mysql_error(&mysql_));
    }
    else
    {
        printf("Update online state successful\n");
    }
}

void ORMManager::insertClient(const char *name, const char *acount, const char *password, const char *usercode, int online, long recently_login, const char *sig_server)
{
    char query[1024];
    sprintf(query, "INSERT INTO clients (USER_NAME, USER_ACOUNT, USER_PASSWD, USER_CODE, USER_ONLINE, USER_RECENTLY_LOGIN, USER_SVR_MOUNT) VALUES ('%s', '%s', '%s', '%s', '%d', '%ld', '%s')",
            name, acount, password, usercode, online, recently_login, sig_server);
    if (mysql_query(&mysql_, query))
    {
        printf("Insert failed: %s\n", mysql_error(&mysql_));
        return;
    }
    else
    {
        printf("Insert successful\n");
    }
}

void ORMManager::deleteClientByUsercode(const char *usercode)
{
    char query[1024];
    sprintf(query, "DELETE FROM clients WHERE USER_CODE = '%s'", usercode);
    if (mysql_query(&mysql_, query))
    {
        printf("Delete failed: %s\n", mysql_error(&mysql_));
        return;
    }
    else
    {
        printf("Delete successful\n");
    }
}

MYSQL_ROW ORMManager::selectClientByUsercode(const char *usercode)
{
    char qeury[1024];
    MYSQL_ROW row;
    MYSQL_RES *res;
    sprintf(qeury, "SELECT * FROM clients WHERE USER_CODE = '%s'", usercode);
    printf("selectClientByUsercode\r\n");
    if (mysql_query(&mysql_, qeury))
    {
        printf("Delete failed: %s\n", mysql_error(&mysql_));
        return NULL;
    }
    else
    {
        res = mysql_store_result(&mysql_);
        if (res)
        {
            row = mysql_fetch_row(res);
            mysql_free_result(res);
            return row;
        }
        printf("Delete successful\n");
    }
    return NULL;
}