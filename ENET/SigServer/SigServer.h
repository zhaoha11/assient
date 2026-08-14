#include "../EdoyunNet/TcpServer.h"

class SigServer : public TcpServer
{
public:
   static std::shared_ptr<SigServer> Create(EventLoop* eventloop);  //设单例
   ~SigServer();
private:
   EventLoop* loop_;
   SigServer(EventLoop* eventloop);
   virtual TcpConnection::Ptr OnConnect(int socket);
};