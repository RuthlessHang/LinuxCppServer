#include <functional>
#include <sys/types.h>
#include <thread>
#include "Server.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "Acceptor.hpp"
#include "Connection.hpp"
#include "EventLoop.hpp"
#include "ThreadPool.hpp"

#define MAX_BUFFER_SIZE 1024
Server::Server(EventLoop *loop) : m_mainReactor(loop), m_acceptor(nullptr), m_thPool(nullptr)
{
    m_acceptor = new Acceptor(m_mainReactor);
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection , this , std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback); //Acceptor 发现有新客户端连接时，通过回调调用newConnection这个函数

    int size = std::thread::hardware_concurrency();   // 线程数 = CPU 核数
    m_thPool = new ThreadPool(size);
    for(int i = 0; i < size; ++i)
    {
        m_subReactors.emplace_back(new EventLoop());
    }

    for(int i = 0; i < size; ++i)
    {
        std::function<void()> task = std::bind(&EventLoop::loop, m_subReactors[i]);
        m_thPool->add(task);
    }
}

Server::~Server()
{
    delete m_acceptor;

    delete m_thPool;    
    for(auto& pair : m_connections)
    {
        delete pair.second;
    }

    for(auto& reactor : m_subReactors)
    {
        delete reactor;
    }
    m_connections.clear();
}

void Server::newConnection(Socket *serverSocket)
{
    InetAddress* clientAddr = new InetAddress();
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    if(clientSocket->getSockfd() == -1)
    {
        delete clientSocket;
        delete clientAddr;
        return;
    }
    clientSocket->setNonBlocking();
    int random = clientSocket->getSockfd() % m_subReactors.size();
    Connection* connection = new Connection(m_subReactors[random], clientSocket);
    std::function<void(Socket*)> callback = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    connection->setDeleteConnectionCallback(callback);
    connection->setOnConnectedCallback(m_onConnectCallback);  // 传业务回调给 Connection
    m_connections[clientSocket->getSockfd()] = connection;
    delete clientAddr;  // 用完释放，防止内存泄漏
}

void Server::deleteConnection(Socket* socket)
{
    if(m_connections.find(socket->getSockfd()) != m_connections.end())
    {
        Connection* connection = m_connections[socket->getSockfd()];
        m_connections.erase(socket->getSockfd());
        delete connection;
    }
}

void Server::OnConnect(std::function<void(Connection*)> cb)
{
    m_onConnectCallback = cb;
}
