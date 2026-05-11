#include "socket.h"
#include "util.h"
#include <unistd.h>
#include "InetAddress.h"
#include <fcntl.h>
#include <iostream>

Socket::Socket() :sockfd(- 1)
{
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd != -1)
	{
		std::cout << "create socket successfully!!!" << std::endl;
	}
	errif(sockfd == -1, "create socket is failed!!!");
}

Socket::Socket(int _sockfd) :sockfd(_sockfd)
{
	if (sockfd != -1)
	{
		std::cout << "create socket successfully!!!" << std::endl;
	}
	errif(sockfd == -1, "create socket is failed!!!");
}

Socket::~Socket()
{
	if(sockfd != -1)
	{
		close(sockfd);
	}
}

void Socket::bind(InetAddress* addr)
{
	int ret = ::bind(sockfd, (sockaddr*)&addr->addr, addr->addr_len);
	if (ret != -1)
	{
		std::cout << "bind socket successfully!!!" << std::endl;
	}
	errif(ret == -1, "bind_failed!");
}

void Socket::listen()
{
	int ret = ::listen(sockfd, SOMAXCONN);
	if (ret != -1)
	{
		std::cout << "listen successfully!!!" << std::endl;
	}
	errif(::listen(sockfd, SOMAXCONN) == -1, "listen failed");
}

int Socket::accept(InetAddress* addr)
{
	int client_sockfd = ::accept(sockfd, (sockaddr*)&addr->addr, &addr->addr_len);
	if (client_sockfd == -1)
	{
		// 如果是 EAGAIN/EWOULDBLOCK 说明没有新连接，正常，不报错
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			return -1;
		}
		// 其他才是真正错误
		errif(true, "accept failed");
	}
	std::cout << "accept successfully!!!" << std::endl;
	return client_sockfd;
}

void Socket::setnonBlock(int sockfd)
{
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
}

int Socket::getSockfd()
{
	return sockfd;
}
