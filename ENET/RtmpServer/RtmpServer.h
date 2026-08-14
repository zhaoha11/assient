#include <mutex>
#include "../EdoyunNet/TcpServer.h"
#include "rtmp.h"
#include "RtmpSession.h"

class RtmpServer : public TcpServer , public Rtmp,public std::enable_shared_from_this<RtmpServer>
{
public:
    using EventCallback = std::function<void(std::string type,std::string streampath)>;
    //创建服务器
    static std::shared_ptr<RtmpServer> Create(EventLoop* eventloop);  //设单例
    ~RtmpServer();
    void SetEventCallback(const EventCallback& cb);
private:
    friend class RtmpConnection;
    RtmpServer(EventLoop* eventloop);
    void AddSesion(std::string stream_path); //每个seesion由流路径来区分
    void RemoveSession(std::string stream_path);

    RtmpSession::Ptr GetSession(std::string stream_path);
    bool HasPublisher(std::string stream_path);
    bool HasSession(std::string stream_path);
    void NotifyEvent(std::string type,std::string stream_path);

    virtual TcpConnection::Ptr OnConnect(int socket);

    EventLoop* loop_;
    std::mutex mutex_;
    std::unordered_map<std::string,RtmpSession::Ptr> rtmp_sessions_;
    std::vector<EventCallback> event_callbacks_;
};