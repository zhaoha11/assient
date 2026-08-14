
#include <stdio.h>
#include "LoadBanceServer.h"
#include "../EdoyunNet/EventLoop.h"

// 8523
// 启动负载 登陆服务器
// int main()
// {
//     EventLoop loop(1);
//     std::shared_ptr<LoadBanceServer> loginServer = nullptr;
//     auto loadServer = LoadBanceServer::Create(&loop);
//     // if(loadServer->Start("192.168.31.30",8523))
//     // {
//     //     loginServer = LoginServer::Create(&loop);
//     //     if(loginServer->Start("192.168.31.30",9867))
//     //     {
//     //         printf("server start succefful\n");
//     //     }
//     // }
//     if(loadServer->Start("192.168.3.130",8523))
//     {
//         loginServer = LoadBanceServer::Create(&loop);
//         if(loginServer->Start("192.168.3.130",9867))
//         {
//             printf("server start succefful\n");
//         }
//     }
//     getchar();
//     return 0;
// }
//启动负载均衡服务器
int main()
{
    EventLoop loop(1);
    auto loadServer = LoadBanceServer::Create(&loop);
    if (!loadServer->Start("192.168.3.130", 8523))
    {
        printf("LoadBanceServer start failed\n");
        return -1;
    }
    printf("LoadBanceServer start successful\n");
    getchar();
    return 0;
}
