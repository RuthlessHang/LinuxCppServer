#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>  
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "util.hpp"

Socket::Socket():m_sockfd(-1)
{
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    //errif(m_sockfd == -1, "socket creation failed");
}

Socket::Socket(int sockfd):m_sockfd(sockfd)
{  
    errif(m_sockfd == -1, "invalid socket file descriptor");
}

Socket::~Socket()
{
    if(m_sockfd != -1)
    {
        close(m_sockfd);
    }
}

void Socket::bind(InetAddress* addr)
{
    errif(::bind(m_sockfd , (struct sockaddr*)&addr->m_addr , addr->m_addr_len) == -1, "bind failed");
}

void Socket::listen()
{
    errif(::listen(m_sockfd, SOMAXCONN) == -1, "listen failed");
}

void Socket::setNonBlocking()
{
    int flags = fcntl(m_sockfd, F_GETFL);
    fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK);
    m_nonBlocking = true;
}

bool Socket::getNonBlocking()
{
    return m_nonBlocking;
}

int Socket::accept(InetAddress* addr)
{
    int client_fd = ::accept(m_sockfd , (struct sockaddr*)&addr->m_addr, &addr->m_addr_len);
    //errif(client_fd == -1, "accept failed");
    return client_fd;
}

void Socket::connect(InetAddress* addr)
{
    errif(::connect(m_sockfd , (struct sockaddr*)&addr->m_addr , addr->m_addr_len) == -1, "connect failed");
}



  int Socket::getSockfd()
  {
    return m_sockfd;
  }