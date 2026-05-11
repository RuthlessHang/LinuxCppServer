#pragma once
#include <arpa/inet.h>   // 网络地址转换（IP/端口字节序转换）

class InetAddress
{
public:
	sockaddr_in addr;
	socklen_t addr_len;
	InetAddress();
	InetAddress(const char* ip, uint16_t port);
	~InetAddress();

};