# 1_SimpleSocket 逻辑总结

## 一、整体架构：最简 TCP 连接建立

这是整个项目的第一阶段，没有类、没有封装，只有两个独立的 `main()` 函数。但它建立了整个项目最核心的架构基石——**TCP 连接建立的四步曲**。

```
服务器 (server.cpp)                    客户端 (client.cpp)
┌─────────────────────┐               ┌─────────────────────┐
│  socket()  → sockfd │               │  socket()  → sockfd │
│       ↓             │               │       ↓             │
│  bind(sockfd)       │               │  connect(sockfd)    │
│       ↓             │               │    → 服务器地址      │
│  listen(sockfd)     │               │                     │
│       ↓             │               │                     │
│  accept(sockfd)     │◄═════════════│  （TCP 三次握手）    │
│    → client_fd      │   网络连接     │                     │
│       ↓             │               │                     │
│  打印客户端 IP       │               │  return 0（退出）   │
│  return 0（退出）   │               │                     │
└─────────────────────┘               └─────────────────────┘
```

### 两个 fd 的区别

这是理解整个项目的第一个关键点：

| fd | 谁创建的 | 作用 | 生命周期 |
|----|---------|------|---------|
| `sockfd`（监听 fd） | `socket()` | 站岗放哨，等客户端连接 | 服务器全生命周期 |
| `client_fd` | `accept()` | 与客户端通信（本阶段未使用） | accept 后程序就退出了 |

**监听 fd 不参与数据收发，通信 fd 不参与接受连接**。这个分离设计贯穿后续所有阶段。

---

## 二、服务器启动流程（四步曲）

TCP 服务器建立的经典四步，所有后续阶段的 Socket 类都在封装这四步：

```
第1步：socket()   → 在内核创建一个 socket 对象，返回 fd
        ↓
第2步：bind()     → 把 fd 与本地地址（IP + 端口）绑定
        ↓         （告诉内核：这个端口收到的连接给我）
第3步：listen()   → 把 socket 标记为"被动监听"状态
        ↓         （开始等客户端 connect）
第4步：accept()   → 从完成队列取出一个连接，返回新的 client_fd
                  （阻塞等待，直到有客户端连进来）
```

对应 [server.cpp](file:///home/yaohang/桌面/MakeCppServer/1_SimpleSocket/server.cpp) 的核心代码：

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);              // 第1步：创建

struct sockaddr_in server_address;                          // 第2步：绑定
server_address.sin_family = AF_INET;
server_address.sin_port = htons(8888);
server_address.sin_addr.s_addr = inet_addr("192.168.153.128");
bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address));

listen(sockfd, SOMAXCONN);                                  // 第3步：监听

int client_socket = accept(sockfd, ...);                    // 第4步：接受连接（阻塞）
```

### TCP 状态变化

```
服务器                           客户端
CLOSED                           CLOSED
  ↓ socket()                       ↓ socket()
CLOSED（有fd了）                  CLOSED（有fd了）
  ↓ bind()                         
CLOSED                           CLOSED
  ↓ listen()                       ↓ connect()
LISTEN ←────── 三次握手 ────────── SYN_SENT
                                    ↓
ESTABLISHED                      ESTABLISHED
  ↓ accept()返回client_fd
（程序退出）
```

---

## 三、客户端连接流程

客户端只有两步，比服务器简单得多：

```
第1步：socket()   → 创建 socket fd
        ↓
第2步：connect()  → 向服务器发起三次握手（阻塞直到完成或失败）
```

对应 [client.cpp](file:///home/yaohang/桌面/MakeCppServer/1_SimpleSocket/client.cpp)：

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);              // 第1步：创建

struct sockaddr_in server_address;                          // 第2步：连接
server_address.sin_family = AF_INET;
server_address.sin_port = htons(8888);
server_address.sin_addr.s_addr = inet_addr("192.168.153.128");
connect(sockfd, (struct sockaddr*)&server_address, sizeof(server_address));
```

**客户端不需要 bind**：`connect` 时内核会自动分配一个临时端口。

---

## 四、完整运行时序

```
[启动服务器] ./server
  ├─ socket() → sockfd
  ├─ bind(sockfd, 192.168.153.128:8888)
  ├─ listen(sockfd)
  └─ accept(sockfd) ← 阻塞在这里，等客户端

[启动客户端] ./client
  ├─ socket() → sockfd
  └─ connect(sockfd, 192.168.153.128:8888)
       ↓
    内核完成三次握手
       ↓
[服务器] accept() 返回，得到 client_socket
  └─ 打印 "Client connected: 192.168.153.128"

[客户端] connect() 返回
  └─ return 0（退出）

[服务器] return 0（退出）
```

**关键**：本阶段 accept 后立刻退出——**没有 read、没有 write、没有循环**。只验证了"连接能建立"这一件事。

---

## 五、设计理念

### 为什么 accept 要返回一个新的 fd

```
sockfd（监听fd）──── 永远只负责 accept，不参与数据收发
       │
       ↓ accept()
client_fd（通信fd）── 负责与客户端 read/write
```

如果让监听 fd 既 accept 又收发数据，就没法同时接受新连接了。**一个 fd 专做一件事**，这个设计贯穿所有后续阶段。

### 地址结构为什么要转换

所有 socket API 接收的是通用的 `sockaddr`，实际填的是 IPv4 的 `sockaddr_in`：

```cpp
struct sockaddr_in server_address;        // IPv4 专用结构
server_address.sin_family = AF_INET;
server_address.sin_port = htons(8888);    // htons：主机字节序 → 网络字节序
server_address.sin_addr.s_addr = inet_addr("192.168.153.128");

bind(sockfd, (struct sockaddr*)&server_address, ...);  // 强转为通用类型
```

- **`htons`**：网络统一用大端序，本机若是小端机必须转换。
- **`inet_addr`**：点分十进制字符串 → 32 位网络字节序整数。

---

## 六、本阶段的局限与演进方向

| 局限 | 后续阶段如何解决 |
|------|-----------------|
| accept 后直接退出，无数据收发 | 2_EchoAndUtil：加 `while(read/write)` 回显循环 |
| 只能接受一个客户端 | 3_Epoll：引入 epoll 多路复用 |
| 几乎无错误处理 | 2_EchoAndUtil：加 `errif` 统一错误检查 |
| 裸 C API，fd 手动管理 | 4_WrapperClass：封装 Socket/Epoll 类（RAII） |

---

## 七、编译与运行

```bash
cd 1_SimpleSocket
make

# 终端 1：启动服务器（阻塞在 accept）
./server

# 终端 2：启动客户端
./client
```

服务器输出：`Client connected: 192.168.153.128`，然后双方都退出。

> IP `192.168.153.128` 硬编码在源码中，需改成自己机器的 IP 或 `127.0.0.1`。

---

## 八、1→8 演进总结

| 阶段 | 核心变化 |
|------|---------|
| **1_SimpleSocket** | **最简 socket：accept 一个客户端就退出** |
| 2_EchoAndUtil | 加 read/write 循环 + errif 错误处理 |
| 3_Epoll | 引入 epoll 多路复用，支持多客户端 |
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| 5_Channel | 引入 Channel 抽象：fd+events+revents 绑定 |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> 这四步曲 `socket → bind → listen → accept` 被后续所有阶段沿用，只是从裸 API 调用逐渐封装进 Socket 类、Acceptor 类中。
