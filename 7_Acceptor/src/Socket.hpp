#ifndef _SOCKET_HPP_
#define _SOCKET_HPP_

class InetAddress;

class Socket
{
public:
    Socket();
    ~Socket();
    Socket(int sockfd);
    int getSockfd();
    void bind(InetAddress& addr);
    void listen();
    void setNonBlocking();
    int accept(InetAddress& addr);
private:
    int m_sockfd;    
};

#endif // _SOCKET_HPP_
