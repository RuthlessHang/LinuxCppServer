#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include "Socket.hpp"
#include "util.hpp"
#include "InetAddress.hpp"
#include "Epoll.hpp"
#include "Channel.hpp"

#define MAX_BUFFER_SIZE 1024

// Set a file descriptor to non-blocking mode
void setNonBlocking(int fd) 
{   
    // fcntl(fd , F_GETFL) :Get the current flags of the file descriptor
    // fcntl(fd , F_GETFL) | O_NONBLOCK :Set the O_NONBLOCK flag (在原有属性基础上，开启非阻塞模式)
    // fcntl(fd , F_SETFL) :Set the flags of the file descriptor
    fcntl(fd , F_SETFL ,fcntl(fd , F_GETFL) | O_NONBLOCK);
}

void handle_read(int sockfd)
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

int main() 
{
    Socket* serverSocket = new Socket();
    InetAddress serverAddr = InetAddress("192.168.48.128" , 8080);
    serverSocket->bind(serverAddr);
    serverSocket->listen();
    std::cout << "Server started !!!!!" << std::endl;

    Epoll* epoll = new Epoll();
    serverSocket->setNonBlocking(); // Set the listening socket to non-blocking mode
    Channel* serverChannel = new Channel(epoll, serverSocket->GetSockfd());
    serverChannel->enableReading();

    while(true)
    {
        std::vector<Channel*> activeChannel = epoll->poll();
        int nfds = activeChannel.size();
        for(int i = 0 ; i < nfds ; i++)
        {
            if(activeChannel[i]->Getfd() == serverSocket->GetSockfd())
            {
                while(true)
                {
                    InetAddress clientAddr;
                    int clientFd = serverSocket->accept(clientAddr);
                    if(clientFd == -1)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) // No more incoming connections
                        {
                            break;
                        }
                        errif(true , "accept error");
                    }
                    setNonBlocking(clientFd); // Set the accepted socket to non-blocking mode
                    Channel* clientChannel = new Channel(epoll, clientFd);
                    clientChannel->enableReading();
                }
            }
            else if(activeChannel[i]->GetRevents() & EPOLLIN)
            {
                handle_read(activeChannel[i]->Getfd());
            }
            else
            {   
                std::cout << "some other event" << std::endl;
            }
        }
    }

    return 0 ;
}