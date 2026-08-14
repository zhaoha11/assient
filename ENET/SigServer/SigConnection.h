#include <map>
#include <vector>
#include <cstdint>
#include "define.h"
#include "../EdoyunNet/TcpConnection.h"

class SigConnection : public TcpConnection
{
public:
    SigConnection(TaskScheduler* scheduler,int socket);
    ~SigConnection();
public:
    bool IsAlive(){return state_ != CLOSE;}
    bool IsNoJion(){return state_ == NONE;}
    bool IsIdle(){return state_ == IDLE;};
    bool IsBusy(){return (state_ == PUSHER || state_ == PULLER);};
    void DisConnected();
    void AddCustom(const std::string& code);
    void RmoveCustom(const std::string& code);
    RoleState GetRoleState()const{return state_;};
    std::string GetCode()const{return code_;};
    std::string GetStreamAddres()const{return streamAddres_;};
protected:
    bool OnRead(BufferReader& buffer);
    void HandleMessage(BufferReader& buffer);
    void Clear();
private:
    void HandleJion(const packet_head* data);
    void HandleObtainStream(const packet_head* data);
    void HandleCreateStream(const packet_head* data);
    void HandleDeleteStream(const packet_head* data);
    void HandleOtherMessage(const packet_head* data);
private:
    void DoObtainStream(const packet_head* data);
    void DoCreateStream(const packet_head* data);
private:
    RoleState state_;
    std::string code_;
    std::string streamAddres_;
    TcpConnection::Ptr conn_ = nullptr;
    std::vector<std::string> objectes_;
};