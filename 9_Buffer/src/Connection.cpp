#include "Connection.hpp"
#include "Channel.hpp"
#include "Socket.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include "Buffer.hpp"
#include "util.hpp"

#define MAX_BUFFER_SIZE 1024

Connection::Connection(EventLoop* loop, Socket* socket)
    : m_loop(loop), m_sock(socket), m_channel(nullptr), read_buffer(nullptr), m_deleteConnectionCallback(nullptr)
{
    m_channel = new Channel(loop, socket->getSockfd());   
    m_channel->setCallback(std::bind(&Connection::handleReadEvent, this ,socket->getSockfd()));
    m_channel->enableReading();
    read_buffer = new Buffer();
}

Connection::~Connection()
{
    delete m_sock;
    delete m_channel;
    delete read_buffer;
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
            read_buffer->append(buffer , bytes_read);
            std::cout << "Received: " << read_buffer->c_str() << std::endl;
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
                ssize_t bytes_written = write(sockfd , read_buffer->c_str() , read_buffer->size());
                if(bytes_written == -1)
                {
                    std::cerr << "socket write failed: " << strerror(errno) << std::endl;
                }
                read_buffer->clear();
                break;
           }
        }
    }
}



void Connection::setDeleteConnectionCallback(std::function<void(Socket* socket)> cb)
{
    m_deleteConnectionCallback = cb;
}
