#pragma once
#include <sys/socket.h>

class InetAddress;

class Socket
{
public:
	
	Socket();
	Socket(int);
	~Socket();
	void bind(InetAddress*);
	void listen();
	int accept(InetAddress*);
	void setnonBlock(int sockfd);
	int getSockfd();

private:
	int sockfd;

};