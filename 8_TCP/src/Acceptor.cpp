#include "Acceptor.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"
#include "Channel.hpp"


Acceptor::Acceptor(EventLoop* loop):m_loop(loop) , m_sock(nullptr) , m_addr(nullptr) , m_acceptChannel(nullptr)
{
    m_sock = new Socket();
    m_addr = new InetAddress("192.168.48.128" , 8888);
    m_sock->bind(*m_addr);
    m_sock->listen();
    m_sock->setNonBlocking();
    m_acceptChannel = new Channel(m_loop , m_sock->getSockfd());
    m_acceptChannel->setCallback(std::bind(&Acceptor::acceptConnection , this));
    m_acceptChannel->enableReading();
}

Acceptor::~Acceptor()
{
    delete m_sock;
    delete m_addr;
    delete m_acceptChannel;
}   

void Acceptor::acceptConnection()
{
    m_newConnectionCallback(m_sock);
}

void Acceptor::setNewConnectionCallback(std::function<void(Socket*)> cb)
{
    m_newConnectionCallback = cb;
}
