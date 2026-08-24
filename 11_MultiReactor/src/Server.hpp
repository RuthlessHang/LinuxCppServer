#ifndef SERVER_HPP
#define SERVER_HPP 
#include <map>
#include <vector>

class Acceptor;
class EventLoop;
class Socket;
class Connection;
class ThreadPool;

class Server
{
public:             
    Server(EventLoop* loop);
    ~Server();

    void newConnection(Socket* serverSocket);
    void deleteConnection(Socket* socket);
private:
    EventLoop* m_mainReactor; 
    Acceptor* m_acceptor;      
    std::map<int, Connection*> m_connections;
    std::vector<EventLoop*> m_subReactors;
    ThreadPool* m_thPool;
};

#endif // SERVER_HPP    