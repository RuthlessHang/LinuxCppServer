#ifndef SERVER_HPP
#define SERVER_HPP
class EventLoop;
class Socket;
class Acceptor;

class Server
{
public:
    Server(EventLoop* loop);
    ~Server();
    void handleReadEvent(int sockfd);
    void newConnection(Socket* serverSocket);
private:
    EventLoop* m_loop;
    Acceptor* m_acceptor;
};

#endif // SERVER_HPP