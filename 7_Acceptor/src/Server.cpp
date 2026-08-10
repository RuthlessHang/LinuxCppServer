#include <iostream>
#include <cstring>
#include <unistd.h>
#include <functional>
#include <errno.h>
#include "Server.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "Epoll.hpp"
#include "Channel.hpp"
#include "Acceptor.hpp"

#define MAX_BUFFER_SIZE 1024

Server::Server(EventLoop *loop):m_loop(loop) , m_acceptor(nullptr)
{
    m_acceptor = new Acceptor(loop);
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection , this , std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback); 
}

Server::~Server()
{
    delete m_acceptor;
}

void Server::handleReadEvent(int sockfd)
{
    char buffer[MAX_BUFFER_SIZE];
    while(true)
    {
        memset(buffer , 0 , sizeof(buffer));
        ssize_t bytes_read = read(sockfd , buffer ,sizeof(buffer));
        if(bytes_read > 0)
        {
            std::cout << "Received: " << buffer << std::endl;
        }
        else if(bytes_read == 0) // Client closed the connection
        {
            std::cout << "Client disconnected" << std::endl;
            close(sockfd);
            break;
        }
        else if(bytes_read == -1)
        {
           if(errno == EINTR) // Interrupted by signal, retry reading
           {
               continue;
           }
           if(errno == EAGAIN || errno == EWOULDBLOCK) // No more data to read
           {
                std::cout << "No more data to read" << std::endl;
                break;
           }
        }
    }
}

void Server::newConnection(Socket *serverSocket)
{
    InetAddress clientAddr;
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    if(clientSocket->getSockfd() != -1)
    {
        clientSocket->setNonBlocking();
        Channel* clientChannel = new Channel(m_loop , clientSocket->getSockfd());
        clientChannel->setCallback(std::bind(&Server::handleReadEvent , this , clientChannel->getFd()));
        clientChannel->enableReading();
    }
}
