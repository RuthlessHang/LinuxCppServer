#ifndef INETADDRESS_HPP
#define INETADDRESS_HPP 
#include <arpa/inet.h>

class InetAddress
{
public:
    struct sockaddr_in m_addr;
    socklen_t m_addr_len;
    InetAddress();
    InetAddress(const char* ip, uint16_t port);
    ~InetAddress();
};

#endif // INETADDRESS_HPP