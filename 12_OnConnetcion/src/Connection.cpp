#include "Connection.hpp"
#include "Channel.hpp"
#include "Socket.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include "Buffer.hpp"
#include <assert.h>

#define MAX_BUFFER_SIZE 1024

Connection::Connection(EventLoop* loop, Socket* socket)
    : m_loop(loop), m_sock(socket)
{
    if(loop != nullptr)
    {
        m_channel = new Channel(loop, socket->getSockfd());   
        m_channel->enableRead();
        m_channel->useET();
    }
    
    m_read_buffer = new Buffer();
    m_write_buffer = new Buffer();

    m_state = State::Connected;
}

Connection::~Connection()
{
    if(m_channel != nullptr)
    {
        delete m_channel;
    }   
    delete m_sock;
    delete m_read_buffer;
    delete m_write_buffer;
}   

void Connection::Read()
{
    assert(m_state == State::Connected);
    m_read_buffer->clear();
    if(m_sock->getNonBlocking()) {
        ReadNonBlocking();
    } else {
        ReadBlocking();
    }
}

void Connection::Write() 
{
    assert(m_state == State::Connected);
    if(m_sock->getNonBlocking()) {
        WriteNonBlocking();
    } else {
        WriteBlocking();
    }
    m_write_buffer->clear();
}

void Connection::ReadNonBlocking()
{
    int sockfd = m_sock->getSockfd();
    char buffer[MAX_BUFFER_SIZE];
    while(true)
    {
        memset(buffer , 0 , sizeof(buffer));
        ssize_t bytes_read = read(sockfd , buffer ,sizeof(buffer));
        if(bytes_read > 0)
        {
            m_read_buffer->append(buffer , bytes_read);
        }
        else if(bytes_read == 0) // Client closed the connection
        {
            std::cout << "Client disconnected" << std::endl;
            m_state = State::Closed;
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
        else 
        {
            std::cout << "other error!" << std::endl;
            m_state = State::Closed;
            break;
        }
    }
}

void Connection::WriteNonBlocking()
{
    int sockfd = m_sock->getSockfd();
    char buf[m_write_buffer->size()];
    memcpy(buf , m_write_buffer->c_str() , m_write_buffer->size());
    int data_size = m_write_buffer->size();
    int data_left = data_size;

    while (data_left > 0) 
    {
        ssize_t bytes_written = write(sockfd , buf + data_size - data_left , data_left);
        if(bytes_written == -1)
        {
            if(errno == EINTR) // Interrupted by signal, retry writing
            {
                continue;
            }
            if(errno == EAGAIN) // No more space to write
            {
                std::cout << "No more space to write" << std::endl;
                break;
            }
            else 
            {
                std::cout << "other error!" << std::endl;
                m_state = State::Closed;
                break;
            }
        }
        data_left -= bytes_written;
    }
}

void Connection::ReadBlocking()
{
    int sockfd = m_sock->getSockfd();
    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
    if(bytes_read > 0) {
        m_read_buffer->append(buffer, bytes_read);
    } else if(bytes_read == 0) {
        m_state = State::Closed;
    } else {
        m_state = State::Closed;
    }
}

void Connection::WriteBlocking()
{
    int sockfd = m_sock->getSockfd();
    ssize_t bytes_written = write(sockfd, m_write_buffer->c_str(), m_write_buffer->size());
    if(bytes_written == -1) {
        m_state = State::Closed;
    }
}

void Connection::setDeleteConnectionCallback(std::function<void(Socket* socket)> cb)
{
    m_deleteConnectionCallback = cb;
}

void Connection::setOnConnectedCallback(std::function<void(Connection* conn)> cb)
{
    m_onConnectedCallback = cb;
    m_channel->setReadCallback([this](){ m_onConnectedCallback(this); });
}

Connection::State Connection::GetState()
{
    return m_state;
}

Socket* Connection::GetSocket()
{
    return m_sock;
}

void Connection::Close()
{
   m_deleteConnectionCallback(m_sock);
}

void Connection::SetSendBuffer(const char* str)
{
    m_write_buffer->setBuffer(str);
}

Buffer* Connection::GetReadBuffer() { return m_read_buffer; }

const char *Connection::ReadBuffer() { return m_read_buffer->c_str(); }

Buffer* Connection::GetWriteBuffer() { return m_write_buffer; }

const char *Connection::WriteBuffer() { return m_write_buffer->c_str(); }

void Connection::GetlineSendBuffer() { m_write_buffer->getLine(); } 





