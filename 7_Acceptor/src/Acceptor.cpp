#include "Acceptor.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "Channel.hpp"


Acceptor::Acceptor(EventLoop* loop):m_loop(loop)
{
    m_Socket = new Socket();
    m_addr = new InetAddress("192.168.48.128" , 8888);
    m_Socket->bind(*m_addr);
    m_Socket->listen();
    m_Socket->setNonBlocking();
    m_acceptChannel = new Channel(m_loop , m_Socket->getSockfd());
    m_acceptChannel->setCallback(std::bind(&Acceptor::acceptConnection , this));
    m_acceptChannel->enableReading();
}

Acceptor::~Acceptor()
{
    delete m_Socket;
    delete m_addr;
    delete m_acceptChannel;
}   

void Acceptor::acceptConnection()
{
    m_newConnectionCallback(m_Socket);
}

void Acceptor::setNewConnectionCallback(std::function<void(Socket*)> cb)
{
    m_newConnectionCallback = cb;
}
