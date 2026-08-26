#ifndef SERVER_HPP
#define SERVER_HPP
#include <map>
#include <vector>
#include <functional>

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

    // 用户注册业务回调的接口
    void OnConnect(std::function<void(Connection*)> cb);

    void newConnection(Socket* serverSocket);
    void deleteConnection(Socket* socket);
private:
    EventLoop* m_mainReactor;
    Acceptor* m_acceptor;
    std::map<int, Connection*> m_connections;
    std::vector<EventLoop*> m_subReactors;
    ThreadPool* m_thPool;
    std::function<void(Connection*)> m_onConnectCallback;  // 用户注册的业务回调
};

#endif // SERVER_HPP