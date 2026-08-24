# 4_WrapperClass — 封装成类

## 这版解决什么问题

把散落在 main 里的裸 socket/epoll API 调用**封装成类**，让代码面向对象、易于维护。

## 新增 3 个类

| 类 | 职责 | 封装了什么 |
|---|------|----------|
| **Socket** | 封装 socket fd + 操作 | `socket()`/`bind()`/`listen()`/`accept()`/`close()` |
| **InetAddress** | 封装 sockaddr_in | IP/port 结构体，构造时自动 memset |
| **Epoll** | 封装 epoll 三件套 | `epoll_create1`/`epoll_ctl`/`epoll_wait` |

## 对比：裸 API vs 类封装

**第 3 版（裸 API）**：
```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
addr.sin_port = htons(8080);
addr.sin_addr.s_addr = inet_addr("127.0.0.1");
bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
listen(sockfd, SOMAXCONN);
int client_fd = accept(sockfd, ...);
int epfd = epoll_create1(0);
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
int nfds = epoll_wait(epfd, event, MAX_EVENTS, -1);
```

**第 4 版（封装后）**：
```cpp
Socket* serverSocket = new Socket();
InetAddress serverAddr = InetAddress("127.0.0.1", 8080);
serverSocket->bind(serverAddr);
serverSocket->listen();
int client_fd = serverSocket->accept(clientAddr);
Epoll* epoll = new Epoll();
epoll->add(serverSocket->GetSockfd(), EPOLLIN | EPOLLET);
std::vector<epoll_event> events = epoll->poll();
```

## Epoll 类的关键改进

- `poll()` 返回 `std::vector<epoll_event>` 而非裸数组，**调用方不用记 nfds**
- `add(fd, events)` 封装了 `epoll_ctl(ADD, ...)`

## 与上一版的差异

| | 3_Epoll | 4_WrapperClass |
|---|--------|---------------|
| 代码风格 | 裸 C API 调用 | C++ 类封装 |
| 资源管理 | 散在 main 里 | 对象生命周期管理 |
| 可读性 | 差 | 好 |
| **核心逻辑** | 不变 | 不变 |

## 这版的限制

- `poll()` 返回的是 `epoll_event` 数组，还是得自己判断 fd 类型
- 业务逻辑仍在 main 里
- 没有「fd + 回调」的抽象，每个 fd 该怎么处理还是要 if/else 判断

## 与下一版的衔接

第 5 版将引入 `Channel` 类，让 epoll 返回 `Channel*`（而非裸 fd），不再需要 if/else 判断。
