#include <iostream>
#include <cstring>
#include <unistd.h>
#include "util.h"
#include <fcntl.h>
#include <errno.h>
#include "Epoll.h"
#include "InetAddress.h"
#include "socket.h"  


#define READ_BUFFER 1024  // 读取缓冲区大小

void handleClientMessage(int client_fd);


int main()
{
    
	Socket *server_socket = new Socket();
	InetAddress* server_addr = new InetAddress("127.0.0.1" ,8888);
	server_socket->bind(server_addr);
	server_socket->listen();
    
	Epoll* epoll = new Epoll();
	int sockfd = server_socket->getSockfd();
    server_socket->setnonBlock(sockfd);  // 设置监听套接字为非阻塞模式
	epoll->addEpollEvent(sockfd, EPOLLIN | EPOLLET);  // 将监听套接字添加到 epoll 实例中，监听可读事件和边缘触发模式

    while (true)
    {
		std::vector<epoll_event> events = epoll->poll();  // 等待事件发生
        int nfds = events.size(); 
        for (int i = 0; i < nfds; ++i)
        {
            if (events[i].data.fd == server_socket->getSockfd())
            {
				InetAddress *client_addr = new InetAddress();
				Socket* client_socket = new Socket(server_socket->accept(client_addr));
                if (client_socket->getSockfd() == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    break;
                }
                else
                {
                    errif(client_socket->getSockfd() == -1, "client_sokcfd is failed!!!");
                }
                client_socket->setnonBlock(client_socket->getSockfd());
				epoll->addEpollEvent(client_socket->getSockfd(), EPOLLIN | EPOLLET);  // 将客户端套接字添加到 epoll 实例中，监听可读事件和边缘触发模式
               
            }
            else if (events[i].events & EPOLLIN)  //接收客户端消息
            {
				handleClientMessage(events[i].data.fd);
            }
            else {        
                std::cout << "something else happen!!!" << std::endl;
            }
        }
        
    }
    delete server_socket;
    delete server_addr;
    return 0;
}

void handleClientMessage(int client_fd)
{
    char buffer[READ_BUFFER];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t recv_bytes = read(client_fd, buffer, sizeof(buffer));
        if (recv_bytes > 0)
        {
            std::cout << "Recv CLient Msg!!!" << std::endl;
            write(client_fd, buffer, sizeof(buffer));
        }
        else if (recv_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            std::cout << "read all!!!" << std::endl;
            break;
        }
        else if (recv_bytes == -1 && errno == EINTR)
        {
            std::cout << "continue recv !!" << std::endl;
            continue;
        }
        else if (recv_bytes == 0)
        {
            std::cout << "Client is disconnect!!!" << std::endl;
            close(client_fd);
            break;
        }
    }
}