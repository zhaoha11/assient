#include "ConnectionManager.h"

std::unique_ptr<ConnectionManager> ConnectionManager::instance_ = nullptr;

ConnectionManager::ConnectionManager()
{
}

ConnectionManager::~ConnectionManager()
{
    Close();
}

ConnectionManager *ConnectionManager::GetInstance()
{
    static std::once_flag flag;
    std::call_once(flag,[&](){
        instance_.reset(new ConnectionManager()); //只会创建一次
    });
    return instance_.get();
}

bool ConnectionManager::AddConn(const std::string &idefy, const TcpConnection::Ptr conn)
{
    if(idefy.empty())
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return connMaps_.emplace(idefy,conn).second;
}

void ConnectionManager::RmvConn(const std::string &idefy)
{
    if(idefy.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connMaps_.find(idefy);
    if(it != connMaps_.end())//查询到
    {
        connMaps_.erase(idefy);
    }
}

TcpConnection::Ptr ConnectionManager::QueryConn(const std::string &idefy)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connMaps_.find(idefy);
    if(it != connMaps_.end())
    {
        return it->second;
    }
    return nullptr;
}

void ConnectionManager::Close()
{
    connMaps_.clear();
}
