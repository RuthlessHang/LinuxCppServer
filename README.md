# Epoll 边缘触发 (ET) 高并发 TCP 服务器

基于 Linux C++ 实现的高性能 TCP 服务器，使用 Epoll 边缘触发 (ET) + 非阻塞 IO 模型，支持高并发客户端连接，是 Linux 高性能网络编程的经典实现。
后续代码在此基础上更新迭代~~~

所有的服务器都是高并发的，可以同时为成千上万个客户端提供服务，这一技术又被称为IO复用。
```
IO复用和多线程有相似之处，但绝不是一个概念。IO复用是针对IO接口，而多线程是针对CPU。
```
当前提交代码版本是单 Reactor 单线程网络服务器经典实现，所有 IO 事件（连接建立、数据可读、连接断开）全部由 epoll 托管，单线程循环处理，无锁、无上下文切换，极致利用单核性能。

IO复用的基本思想是事件驱动，服务器同时保持多个客户端IO连接，当这个IO上有可读或可写事件发生时，表示这个IO对应的客户端在请求服务器的某项服务，此时服务器响应该服务。在Linux系统中，
IO复用使用select, poll和epoll来实现。epoll改进了前两者，更加高效、性能更好，是目前几乎所有高并发服务器的基石。请读者务必先掌握epoll的原理再进行编码开发。

完整执行流程：

    1.初始化阶段 :创建 TCP 监听 Socket → 绑定 IP 端口 → 开启监听 → 设置非阻塞模式 → 创建 epoll 实例 → 将监听 Socket 加入 epoll 监听队列;
    2.事件循环阶段（核心死循环）:阻塞等待 epoll 内核返回就绪事件 → 遍历所有就绪事件 → 分场景处理两类核心事件;
    3.事件处理阶段:(1) 监听 Socket 就绪：循环 accept 接收所有新连接，新连接设置非阻塞并加入 epoll 监听;
                  (2) 客户端 Socket 就绪：循环 read 读完所有缓冲区数据，完成数据回显;
                  (3) 客户端断开连接：从 epoll 移除描述符，关闭sockfd，释放资源;


核心设计原则

    ET 模式必须搭配非阻塞 IO：边缘触发仅在状态变更时通知一次，必须一次性读完 / 处理完所有事件，绝对不能使用阻塞 IO;
    ET 模式必须循环处理：accept、read 都要循环调用，直到返回EAGAIN/EWOULDBLOCK，避免遗漏连接和数据;
    文件描述符生命周期管理：连接创建时加入 epoll，断开时必须从 epoll 删除并关闭，避免句柄泄漏;


接下来，通过分模块结合源码逐行详解

模块 1：头文件与全局常量定义

    #include <iostream>
    #include <cstring>
    #include <sys/socket.h>  // socket 核心系统调用（Linux 系统头文件，C/C++ 通用）
    #include <arpa/inet.h>   // 网络地址转换（IP/端口字节序转换）
    #include <unistd.h>   // // Linux 核心系统调用（close / read / write）
    #include "util.h"     //自定义错误检查函数
    #include <fcntl.h>   //文件描述符控制（设置非阻塞 O_NONBLOCK）
    #include <sys/epoll.h>  // epoll 多路复用核心 API
    #include <errno.h>  // 系统错误码（errno / EAGAIN / EINTR）
    
    #define MAX_EVENTS 1024  // 最多同时监听 1024 个事件（客户端连接）
    #define READ_BUFFER 1024  // 读取缓冲区大小

所有头文件均为 Linux 系统原生头文件，无第三方依赖，可直接编译

模块 2：非阻塞文件描述符设置函数

    void setnonBlock(int fd)
    {
        // 获取fd原有状态标记，追加O_NONBLOCK非阻塞标记，重新设置给fd
        fcntl(fd , F_SETFL ,fcntl(fd , F_GETFL)|O_NONBLOCK);
    }

这是 epoll ET 模式的必备前置函数，所有加入 epoll 的文件描述符（监听 Socket、客户端 Socket）都必须设置非阻塞
通过fcntl修改文件描述符状态，不改变原有标记，仅新增非阻塞属性

模块 3：服务端 Socket 初始化与监听

     // 1. 创建TCP流式套接字，IPv4协议族，返回Socket文件描述符
    int sockfd = socket(AF_INET , SOCK_STREAM ,0);

    // 2. 配置服务端地址结构体，清零后赋值
    sockaddr_in server_addr;
    memset(&server_addr , 0 ,sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4协议
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 监听本地回环地址
    server_addr.sin_port = htons(8888);        // 监听端口，主机字节序转网络字节序

    // 3. 绑定Socket与IP端口，失败直接报错退出
    errif(bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0, "bind_failed!");
    // 4. 将Socket设置为监听状态，最大半连接队列内核默认最大值
    errif(listen(sockfd, SOMAXCONN) != 0, "listen failed");

TCP 服务端标准初始化四步：socket → bind → listen，固定流程
htons：端口必须从主机字节序转为网络字节序（大端序），否则端口无法正常监听
SOMAXCONN：Linux 内核定义的半连接队列最大长度，无需手动硬编码

模块 4：epoll 实例初始化与监听 Socket 注册

    // 1. 创建epoll内核事件表，参数为内核提示大小，无严格限制
    int epfd = epoll_create(1024);
    errif(epfd == -1 , "create epoll is failed!");

    // 2. 定义事件结构体数组：events用于接收内核返回的就绪事件，ev用于注册事件
    epoll_event events[MAX_EVENTS], ev;
    memset(&events , 0 , sizeof(events));
    
    // 3. 配置监听事件：可读事件EPOLLIN + 边缘触发EPOLLET
    memset(&ev , 0 ,sizeof(ev));
    ev.data.fd = sockfd;  // 绑定对应的文件描述符
    ev.events = EPOLLIN | EPOLLET;

    // 4. 监听Socket设置非阻塞，加入epoll监听队列
    setnonBlock(sockfd); 
    epoll_ctl(epfd , EPOLL_CTL_ADD , sockfd , &ev);

epoll_create：创建内核级事件表，所有需要监听的描述符都注册到这里;
epoll_ctl：epoll 唯一的操作函数，支持三种操作:

    EPOLL_CTL_ADD：新增描述符到 epoll
    EPOLL_CTL_MOD：修改已注册描述符的监听事件
    EPOLL_CTL_DEL：从 epoll 删除描述符
                
核心事件配置：EPOLLIN（监听可读事件，新连接、客户端数据都属于可读事件）+ EPOLLET（开启边缘触发模式）

模块 5：epoll 核心事件循环

     int nfds = epoll_wait(epfd , events , MAX_EVENTS , -1); // 阻塞等待就绪事件，nfds为本次内核返回的就绪事件总数
     
模块 6：新客户端连接处理（监听 Socket 事件）

     //水平触发 (LT) 默认会重复通知未处理完的事件，边缘触发 (ET) 仅通知一次，因此必须一次性处理完所有事件
       if (events[i].data.fd == sockfd)
         {
             while (1)   // ET模式必须循环accept，直到返回EAGAIN，否则会遗漏连接
             {
                 sockaddr_in client_addr;
                 socklen_t client_addr_len = sizeof(client_addr);
                 memset(&client_addr, 0, sizeof(client_addr));
        
                 int client_sockfd = accept(sockfd, (sockaddr*)&client_addr, &client_addr_len);  // 接收客户端连接，返回客户端专属Socket描述符
                 if (client_sockfd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))  // 非阻塞模式下，没有新连接了，退出循环
                 {
                     break;
                 }
                 else
                 {
                     errif(client_sockfd == -1 , "client_sokcfd is failed!!!");  // accept真正失败，报错退出
                 }
                  // 新客户端Socket配置：非阻塞 + ET边缘触发 + 加入epoll监听
                 memset(&ev, 0, sizeof(ev));
                 ev.data.fd = client_sockfd;
                 ev.events = EPOLLIN | EPOLLET;
                 setnonBlock(client_sockfd);
                 epoll_ctl(epfd, EPOLL_CTL_ADD, client_sockfd, &ev);
             }             
         } 
ET 模式核心要点：监听 Socket 触发可读事件后，内核只会通知一次，必须用while(1)循环调用accept，把队列里所有连接全部接收完，直到返回EAGAIN，否则会遗漏连接;
新连接必须和监听 Socket 保持一致配置：非阻塞 + ET 模式，否则会出现阻塞、事件不触发的问题;
新连接创建后立即加入 epoll，后续客户端发送数据时，epoll 会自动触发可读事件;

模块 7：客户端数据读取与回显

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

同样遵循ET 模式循环读取规则：内核仅通知一次可读事件，必须循环 read 把缓冲区数据全部读完，否则剩余数据不会再次触发事件;
完整的错误码处理，覆盖所有网络 IO 异常场景：

    EAGAIN/EWOULDBLOCK：非阻塞模式下无数据可读，正常结束
    EINTR：被系统信号中断，无需报错，重新读取即可
    recv_bytes == 0：TCP 正常断开标志，客户端主动关闭连接，必须清理资源
    
业务逻辑：极简回显服务，读到数据直接原样写回客户端，可自行替换为自定义业务逻辑;


编译与运行命令:
    我这里是直接通过vs远程连接虚拟机，直接生成服务端的exe，相关配置这里不再赘述，生成服务端命令在makefile
    客户端直接在虚拟机里面进行编译后启动，最终实现服务端与多个客户端双方完成正常通信
