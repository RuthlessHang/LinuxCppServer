#pragma once    
#include <functional>

class EventLoop;
class Channel;
class Socket;
class InetAddress;

class Acceptor
{
public:
    Acceptor(EventLoop* loop);
    ~Acceptor();
    void acceptConnection(); 
    void setNewConnectionCallback(std::function<void(Socket*)> cb);
private:
    Channel* m_acceptChannel;
    Socket* m_sock;
    InetAddress* m_addr;
    EventLoop* m_loop;
    std::function<void(Socket*)> m_newConnectionCallback;
};