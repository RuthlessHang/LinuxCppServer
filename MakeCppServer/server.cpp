#include <iostream>
#include <cstring>
#include <sys/socket.h>  // socket 核心系统调用（Linux 系统头文件，C/C++ 通用）
#include <arpa/inet.h>   // 网络地址转换（IP/端口字节序转换）
#include <unistd.h>
#include "util.h"
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_EVENTS 1024  // 最多同时监听 1024 个事件（客户端连接）
#define READ_BUFFER 1024  // 读取缓冲区大小

// 功能：把文件描述符 fd 设置为【非阻塞模式】 ->  非阻塞 = 调用 read/write/accept 不会卡住程序
void setnonBlock(int fd)
{
    fcntl(fd , F_SETFL ,fcntl(fd , F_GETFL)|O_NONBLOCK);
}

int main()
{
     //创建 TCP 套接字 
    // AF_INET：IPv4 协议族
    // SOCK_STREAM：TCP 协议（面向连接、可靠传输）
    // 0：默认协议（TCP 协议填 0 即可）
    // 返回值：成功返回套接字文件描述符（非负整数），失败返回 -1
    int sockfd = socket(AF_INET , SOCK_STREAM ,0);

    //配置服务器地址结构体
    sockaddr_in server_addr;
    memset(&server_addr , 0 ,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(8888);

    //绑定套接字与 IP+端口
    errif(bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0, "bind_failed!");
    //将套接字设为监听状态 
    errif(listen(sockfd, SOMAXCONN) != 0, "listen failed");
    
    // 创建 epoll 实例（内核事件表）
    int epfd = epoll_create(1024);
    errif(epfd == -1 , "create epoll is failed!");

    // 接收内核返回的事件
    epoll_event events[MAX_EVENTS], ev;
    memset(&events , 0 , sizeof(events));
    
    // 监听事件：EPOLLIN（有新连接）+ EPOLLET（边缘触发）
    memset(&ev , 0 ,sizeof(ev));
    ev.data.fd = sockfd;
    ev.events = EPOLLIN | EPOLLET;
    //阻塞接受客户端连接 ,// 添加到 epoll
    setnonBlock(sockfd); 
    epoll_ctl(epfd , EPOLL_CTL_ADD , sockfd , &ev);

    //epoll 主循环：不断等待并处理事件
    while (true)
    {
        int nfds = epoll_wait(epfd , events , MAX_EVENTS , -1); 
        for (int i = 0; i < nfds; ++i)
        {
            if (events[i].data.fd == sockfd)
            {
                while (1)
                {
                    sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    memset(&client_addr, 0, sizeof(client_addr));

                    int client_sockfd = accept(sockfd, (sockaddr*)&client_addr, &client_addr_len);
                    if (client_sockfd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        break;
                    }
                    else
                    {
                        errif(client_sockfd == -1 , "client_sokcfd is failed!!!");
                    }
                    memset(&ev, 0, sizeof(ev));
                    ev.data.fd = client_sockfd;
                    ev.events = EPOLLIN | EPOLLET;
                    setnonBlock(client_sockfd);
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_sockfd, &ev);
                }
                
            }
            else if (events[i].events & EPOLLIN)  //接收客户端消息
            {
                while (true)
                {
                    char buffer[1024];
                    memset(buffer, 0, sizeof(buffer));

                    ssize_t recv_bytes = read(events[i].data.fd, buffer, sizeof(buffer));
                    if (recv_bytes > 0)
                    {
                        std::cout << "Recv CLient Msg!!!" << std::endl;
                        write(events[i].data.fd, buffer, sizeof(buffer));
                    }
                    else if (recv_bytes == -1 &&(errno == EAGAIN || errno == EWOULDBLOCK))
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
                        epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                        close(events[i].data.fd);
                        break;
                    }
                }
            }
            else {        
                std::cout << "something else happen!!!" << std::endl;
            }
        }
        
    }

    close(sockfd);
    return 0;
}
