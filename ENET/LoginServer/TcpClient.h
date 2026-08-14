#include "define.h"

class TcpClient
{
public:
	TcpClient();
	void Create();
	bool Connect(std::string ip, uint16_t port);
	void Close();
	~TcpClient();
    void getMonitorInfo();  
protected:
    void get_mem_usage();
	void Send(uint8_t* data, uint32_t size);
private:
	FILE* file_;
	bool isConnect_;
	int sockfd_;
	struct sysinfo info_;
	Monitor_body Monitor_info_;
};