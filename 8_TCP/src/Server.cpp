#include <functional>
#include "Server.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "Acceptor.hpp"
#include "Connection.hpp"

#define MAX_BUFFER_SIZE 1024

Server::Server(EventLoop *loop):m_loop(loop) , m_acceptor(nullptr)
{
    m_acceptor = new Acceptor(loop);
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection , this , std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback); //Acceptor 发现有新客户端连接时，通过回调调用newConnection这个函数
}

Server::~Server()
{
    delete m_acceptor;
    for(auto& pair : m_connections)
    {
        delete pair.second;
    }
    m_connections.clear();
}

void Server::newConnection(Socket *serverSocket)
{
    InetAddress clientAddr;
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    if(clientSocket->getSockfd() == -1)
    {
        delete clientSocket;
        return;
    }
    clientSocket->setNonBlocking();
    Connection* connection = new Connection(m_loop, clientSocket);
    std::function<void(Socket*)> callback = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    connection->setDeleteConnectionCallback(callback);
    m_connections[clientSocket->getSockfd()] = connection;
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
