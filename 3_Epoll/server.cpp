#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include "util.hpp"

#define MAX_EVENTS 100
#define MAX_BUFFER_SIZE 1024

// Set a file descriptor to non-blocking mode
void setNonBlocking(int fd) 
{   
    // fcntl(fd , F_GETFL) :Get the current flags of the file descriptor
    // fcntl(fd , F_GETFL) | O_NONBLOCK :Set the O_NONBLOCK flag (在原有属性基础上，开启非阻塞模式)
    // fcntl(fd , F_SETFL) :Set the flags of the file descriptor
    fcntl(fd , F_SETFL ,fcntl(fd , F_GETFL) | O_NONBLOCK);
}

int main() 
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(sockfd == -1, "socket creation failed");
    
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = inet_addr("192.168.48.128");

    errif(bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1, "bind failed");
    errif(listen(sockfd, SOMAXCONN) == -1, "listen failed");
    std::cout << "Server started !!!!!" << std::endl;

    int epfd = epoll_create1(0);
    errif(epfd == -1, "epoll_create1 failed");

    struct epoll_event event[MAX_EVENTS] ,ev ;
    memset(event , 0 , sizeof(event));
    memset(&ev , 0 , sizeof(ev));
    ev.events = EPOLLIN | EPOLLET; // Edge Triggered
    ev.data.fd = sockfd;
    setNonBlocking(sockfd); // Set the listening socket to non-blocking mode
    errif(epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1, "epoll_ctl failed");

    while(true)
    {
       int  nfds = epoll_wait(epfd, event, MAX_EVENTS, -1);
       errif(nfds == -1, "epoll_wait failed");
       for(int i = 0 ; i < nfds ; i++)
       {
            if(event[i].data.fd == sockfd)
            {
                struct sockaddr_in client_address;
                memset(&client_address , 0 , sizeof(client_address));
                socklen_t client_len = sizeof(client_address);
                int client_fd = accept(sockfd, (struct sockaddr*)&client_address, &client_len);
                errif(client_fd == -1, "accept failed");

                memset(&ev , 0 , sizeof(ev));
                ev.events = EPOLLIN | EPOLLET; // Edge Triggered
                ev.data.fd = client_fd;  
                setNonBlocking(client_fd);   
                epoll_ctl(epfd , EPOLL_CTL_ADD, client_fd, &ev); 
            }
            else if(event[i].events & EPOLLIN)
            {
                while(true)
                {
                    char buffer[MAX_BUFFER_SIZE];
                    memset(buffer , 0 , sizeof(buffer));
                    int bytes_read = read(event[i].data.fd, buffer, sizeof(buffer));
                    if(bytes_read > 0)
                    {
                        std::cout << "Received: " << buffer << std::endl;
                        // Echo the message back to the client
                        write(event[i].data.fd , buffer , sizeof(buffer)); //write(fd, buf, bytes_read); 只发送本次读到的字节数就不需要限制客户端buf大小
                       
                    }
                    //Interrupt system call：系统调用被Linux 信号中断
                    else if(bytes_read == -1 && errno == EINTR)
                    {
                        // Interrupted by signal, try again
                        continue;
                    }
                    //E = Error（错误码），AGAIN = 再来一次  , EAGAIN :暂时没数据，稍后重试;
                    //WOULD BLOCK = 将会阻塞 , EWOULDBLOCK :非阻塞模式下，缓冲区没有数据可读
                    else if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        std::cout << "No more data to read" << std::endl;
                        break;
                    }
                    else if(bytes_read == 0)
                    {
                        // Client disconnected
                        std::cout << "Client disconnected: fd " << event[i].data.fd << std::endl;
                        close(event[i].data.fd);
                        break;
                    }
                }
                
            }
        }

    } 

    close(sockfd);
    return 0;
}