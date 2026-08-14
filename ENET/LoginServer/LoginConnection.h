#include "../EdoyunNet/TcpConnection.h"
#include "define.h"
#include <string>
class LoginConnection : public TcpConnection
{
public:
    LoginConnection(TaskScheduler *scheduler, int socket);
    ~LoginConnection();

protected:
    bool IsTimeout(uint64_t timestamp);
    bool OnRead(BufferReader &buffer);
    void HandleMessage(BufferReader &buffer);
    void Clear();

private:
    void HandleRegister(const packet_head *data);
    void HandleLogin(const packet_head *data);
    void HandleDestory(const packet_head *data);

private:
    std::string user_code_;
    bool logged_in_ = false;
};