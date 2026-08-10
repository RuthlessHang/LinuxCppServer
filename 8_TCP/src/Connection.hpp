#pragma once
#include <functional>
class EventLoop;
class Socket;
class Channel;

class Connection
{
public:
    Connection(EventLoop* loop, Socket* socket);
    ~Connection();
    void setDeleteConnectionCallback(std::function<void(Socket* socket)> cb);
    void handleReadEvent(int sockfd);
private:
    EventLoop* m_loop;
    Socket* m_sock;
    Channel* m_channel;
    std::function<void(Socket* socket)> m_deleteConnectionCallback;
};