#ifndef SERVER_HPP
#define SERVER_HPP 
#include <map>

class Acceptor;
class EventLoop;
class Socket;
class Connection;

class Server
{
public:             
    Server(EventLoop* loop);
    ~Server();

    void newConnection(Socket* serverSocket);
    void deleteConnection(Socket* socket);
private:
    EventLoop* m_loop; 
    Acceptor* m_acceptor;      
    std::map<int, Connection*> m_connections;
};

#endif // SERVER_HPP    