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
    epoll->add(serverSocket->GetSockfd(), EPOLLIN | EPOLLET); // Edge Triggered
    
    while(true)
    {
        std::vector<epoll_event> events = epoll->poll();
        int nfds = events.size();
        for(int i = 0 ; i < nfds ; i++)
        {
            if(events[i].data.fd == serverSocket->GetSockfd())   //新客户端连接
            {
                while(true)
                {
                    InetAddress clientAddr;
                    int client_fd = serverSocket->accept(clientAddr);
                    if(client_fd == -1) 
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        errif(true,"accept 接收客户端失败");
                    }
                    setNonBlocking(client_fd); // Set the client socket to non-blocking mode
                    epoll->add(client_fd, EPOLLIN | EPOLLET); // Add the client socket to epoll

                }
                
            }
            else if(events[i].events & EPOLLIN)
            {
                handle_read(events[i].data.fd);
            }
            else
            {
                std::cout << "Unexpected event" << std::endl;
            }
        }
    }

    return 0 ;
}