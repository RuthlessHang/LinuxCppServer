#include "Connection.hpp"
#include "Channel.hpp"
#include "Socket.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <errno.h>

#define MAX_BUFFER_SIZE 1024

Connection::Connection(EventLoop* loop, Socket* socket)
    : m_loop(loop), m_sock(socket), m_channel(nullptr), m_deleteConnectionCallback(nullptr)
{
    m_channel = new Channel(loop, socket->getSockfd());
    m_channel->setCallback(std::bind(&Connection::handleReadEvent, this ,socket->getSockfd()));
    m_channel->enableReading();
}

Connection::~Connection()
{
    delete m_sock;
    delete m_channel;
}   

void Connection::handleReadEvent(int sockfd)
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
            if(m_deleteConnectionCallback)
            {
                m_deleteConnectionCallback(m_sock);
            }
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



void Connection::setDeleteConnectionCallback(std::function<void(Socket* socket)> cb)
{
    m_deleteConnectionCallback = cb;
}
