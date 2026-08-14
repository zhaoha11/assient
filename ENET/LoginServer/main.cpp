#include <stdio.h>
#include "../EdoyunNet/EventLoop.h"
#include "LoginServer.h"
// #include <jsoncpp/json/json.h>

int main()
{
    // Json::Value root;
    // root["name"] = "John";
    // root["age"] = 30;
    // root["married"] = true;
    // Json::Reader gh;
    // return 0;
    int count = std::thread::hardware_concurrency();
    EventLoop loop(1);
    auto login_server = LoginServer::Create(&loop);
    // if(!login_server->Start("192.168.31.30",6539))
    if (!login_server->Start("192.168.3.130", 9867))
    {
        printf("LoginServer server failed\n");
    }
    printf("LoginServer server success\n");
    getchar();
    return 0;
}