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
    void bind(InetAddress* addr);
    void listen();
    void setNonBlocking();
    bool getNonBlocking();
    int accept(InetAddress* addr);
    void connect(InetAddress* addr);
private:
    int m_sockfd;
    bool m_nonBlocking = false;
};

#endif // _SOCKET_HPP_
