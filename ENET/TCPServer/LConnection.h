#include "../EdoyunNet/TcpConnection.h"

class LConnection : public TcpConnection
{
public:
    LConnection(TaskScheduler* scheduler,int socket);
    ~LConnection();
protected:
    bool OnRead(BufferReader& buffer);
    void HandleMessage(BufferReader& buffer);
    void Clear();
};