#include "../EdoyunNet/TcpServer.h"

class EchoServer : public TcpServer
{
public:
   static std::shared_ptr<EchoServer> Create(EventLoop* eventloop); 
   ~EchoServer();
protected:
    void Clear();
private:
   EventLoop* loop_;
   EchoServer(EventLoop* eventloop);
   virtual TcpConnection::Ptr OnConnect(int socket); 
};