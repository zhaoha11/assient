#include "TcpClient.h"
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

TcpClient::TcpClient()
    :sockfd_(-1)
    ,file_(nullptr)
    ,isConnect_(false)
{
}

void TcpClient::Create()
{
    //获取内存
    file_ = fopen("/proc/meminfo","r");
    if(!file_)
    {
        printf("open file fialed\n");
        return;
    }
    memset(&info_,0,sizeof(info_));

    //初始化这个info
    sysinfo(&info_);
    //创建socket
    sockfd_ = ::socket(AF_INET,SOCK_STREAM,0);
    Monitor_info_.SetIp("192.168.3.130");
    Monitor_info_.port = 9867;
}

bool TcpClient::Connect(std::string ip, uint16_t port)
{
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    int len = sizeof(sockaddr_in);

    //连接
    if(::connect(sockfd_,(sockaddr*)&addr,len) == -1)
    {
        printf("connect error\n");
        return false;
    }
    isConnect_ = true;
    return true;
}

void TcpClient::Close()
{
    isConnect_ = false;
    fclose(file_);
    if(sockfd_)
    {
        ::close(sockfd_);
    }
}

TcpClient::~TcpClient()
{
    Close();
}

void TcpClient::getMonitorInfo()
{
    get_mem_usage();
}

void TcpClient::get_mem_usage()
{
    //获取内存再去发送
    size_t bytes_used;
    size_t read;
    char* line = NULL;
    int index = 0;
    int avimem = 0;

    while (read = getline(&line,&bytes_used,file_) != -1)
    {
        if(++index <= 2)
        {
            continue;
        }

        if(strstr(line,"MemAvailable") != NULL)
        {
            sscanf(line,"%*s%d%*s",&avimem);
            break;
        }
    }

    int t = info_.totalram / 1024.0;
    double mem = (t - avimem) * 100 / t;
    Monitor_info_.mem = (uint8_t)mem;
    Send((uint8_t*)&Monitor_info_,Monitor_info_.len);
}

void TcpClient::Send(uint8_t *data, uint32_t size)
{
    int len = 0;
    int index = 0;
    while(index != size)
    {
        len = ::send(sockfd_,data + index ,size - index,0);
        if(len < 0)
        {
            break;
        }
        index += len;
    }
}
