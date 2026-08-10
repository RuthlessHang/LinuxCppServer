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

#define MAX_BUFFER_SIZE 1024

Server::Server(EventLoop *loop):m_loop(loop)
{
    Socket* serverSocket = new Socket();
    InetAddress serverAddr = InetAddress("192.168.48.128" , 8888);
    serverSocket->bind(serverAddr);
    serverSocket->listen();
    serverSocket->setNonBlocking();
    Channel* serverChannel = new Channel(m_loop , serverSocket->getSockfd());

    std::function<void()> callback = std::bind(&Server::newConnection , this , serverSocket);
    serverChannel->setCallback(callback);
    serverChannel->enableReading();
}

Server::~Server()
{
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
        // std::function<void()> callback = std::bind(&Server::handleReadEvent , this , clientChannel->getFd());
        // clientChannel->setCallback(callback);
        clientChannel->setCallback(std::bind(&Server::handleReadEvent , this , clientChannel->getFd()));
        clientChannel->enableReading();
    }
}
