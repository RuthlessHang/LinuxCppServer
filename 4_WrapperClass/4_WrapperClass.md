# 4_WrapperClass 逻辑总结

## 一、类与类的包含关系

本阶段第一次引入面向对象，把阶段 3 中散落在 `main()` 里的裸 C API 封装成 C++ 类。架构从"一堆系统调用"变成"对象之间的协作"。

```
main()
  ├── Socket* serverSocket        （监听fd的RAII封装）
  │     └── m_sockfd              （构造时 socket()，析构时 close()）
  │
  ├── InetAddress serverAddr      （地址封装：IP + Port）
  │     └── m_addr (sockaddr_in)
  │
  ├── Epoll* epoll                （epoll实例的封装）
  │     ├── m_epoll_fd            （构造时 epoll_create1()）
  │     └── m_events[]            （事件数组，epoll_wait 的缓冲区）
  │
  └── while(true) 事件循环
        ├── epoll->poll()         → 返回 vector<epoll_event>
        └── for 每个事件：
              ├── if 是监听fd → serverSocket->accept() + epoll->add(客户端fd)
              └── else → handle_read(客户端fd)
```

### 每个类的职责

| 类 | 职责 | 存了什么 | RAII |
|----|------|---------|------|
| `Socket` | 封装 socket fd 的生命周期 | m_sockfd | 构造 socket()，析构 close(fd) |
| `InetAddress` | 封装地址结构 | m_addr, m_addr_len | 无（栈对象） |
| `Epoll` | 封装 epoll 实例 | m_epoll_fd, m_events[] | 构造 epoll_create1()，析构 close + delete[] |
| `util` | 错误处理工具 | 无（全局函数） | — |

### RAII 资源管理

这是本阶段引入的核心理念——**构造即获取资源，析构即释放资源**：

```
Socket 的生命周期：
  new Socket()     → socket() 创建 fd    ← 获取资源
  bind / listen    → 使用 fd
  accept           → 使用 fd
  ~Socket()        → close(fd)           ← 自动释放资源

Epoll 的生命周期：
  new Epoll()      → epoll_create1() + new events[]  ← 获取资源
  add / poll       → 使用 epfd
  ~Epoll()         → close(epfd) + delete[] events   ← 自动释放资源
```

只要对象生命周期结束，fd 自动关闭，无需手动 `close`。

---

## 二、启动流程

### 第 1 步：main.cpp 创建监听 Socket

```cpp
Socket* serverSocket = new Socket();                    // socket() 创建fd
InetAddress serverAddr = InetAddress("192.168.48.128", 8080);
serverSocket->bind(serverAddr);                         // bind
serverSocket->listen();                                 // listen
```

对比阶段 3 的裸 API：

```cpp
// 阶段 3（裸 API）
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(8080);
addr.sin_addr.s_addr = inet_addr("192.168.48.128");
bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
listen(sockfd, SOMAXCONN);

// 阶段 4（封装后）
Socket* serverSocket = new Socket();
InetAddress serverAddr("192.168.48.128", 8080);  // 一行搞定地址
serverSocket->bind(serverAddr);                   // 一行搞定 bind
serverSocket->listen();
```

### 第 2 步：创建 Epoll 并注册监听 fd

```cpp
Epoll* epoll = new Epoll();                              // epoll_create1()
serverSocket->setNonBlocking();                           // ET 模式要求非阻塞
epoll->add(serverSocket->GetSockfd(), EPOLLIN | EPOLLET); // 注册监听fd
```

### 第 3 步：事件循环

```cpp
while(true)
{
    std::vector<epoll_event> events = epoll->poll();   // epoll_wait
    for(int i = 0; i < events.size(); i++)
    {
        if(events[i].data.fd == serverSocket->GetSockfd())  // 监听fd
            // accept + 注册新客户端fd
        else if(events[i].events & EPOLLIN)                 // 客户端fd
            handle_read(events[i].data.fd);
    }
}
```

---

## 三、运行时流程

### 新客户端连接

```
客户端 connect()
  ↓
内核：监听fd可读
  ↓
epoll->poll() 返回 events[0].data.fd == serverSocket->GetSockfd()
  ↓
while(true) {                           ← ET 模式要循环 accept
  InetAddress clientAddr;               ← 默认构造，等 accept 回填
  int client_fd = serverSocket->accept(clientAddr);
  if(client_fd == -1 && errno == EAGAIN) break;  ← 接完了
  setNonBlocking(client_fd);
  epoll->add(client_fd, EPOLLIN | EPOLLET);      ← 新fd注册进epoll
}
```

### 客户端发数据

```
客户端 write("hello")
  ↓
内核：客户端fd可读
  ↓
epoll->poll() 返回 events[0].data.fd == client_fd
  ↓
handle_read(client_fd)                   ← 仍然是全局函数，不是类方法
  └─ while(true) {
       read → "hello" → 打印
       read → -1, EAGAIN → break         ← ET 模式循环读到空
     }
```

---

## 四、关键设计：data.fd vs data.ptr

本阶段 Epoll 用 `ev.data.fd = fd` 存 fd，poll 返回 `epoll_event`。主循环靠 `events[i].data.fd == serverSocket->GetSockfd()` 比较 fd 来区分类型。

```
注册时：ev.data.fd = sockfd         ← 存的是 fd 整数
返回时：events[i].data.fd           ← 拿到的是 fd 整数
        → 需要 if(fd == 监听fd) 比较  ← 麻烦！
```

这是本阶段的局限，也是下一阶段引入 Channel 的直接动机。5_Channel 会改为 `ev.data.ptr = channel`，poll 直接返回 Channel 指针，省去 fd 比较。

---

## 五、设计理念

### 为什么要封装成类

阶段 3 的痛点：

```cpp
// 阶段 3：fd 手动管理，容易遗漏
int sockfd = socket(...);
int epfd = epoll_create1(0);
// ... 使用 ...
close(sockfd);   ← 忘了就泄漏
close(epfd);     ← 忘了就泄漏
```

封装后的好处：

1. **RAII**：Socket 构造时创建 fd，析构时自动 close。即使忘记 delete，对象析构也会兜底。
2. **封装**：InetAddress 把 `memset + 填三字段` 藏进构造函数，调用方一行搞定。
3. **可读性**：`serverSocket->bind(serverAddr)` 比 `bind(sockfd, (struct sockaddr*)&addr, sizeof(addr))` 直观。

### Socket 类的两个构造函数

```
Socket()        → socket() 创建新fd（服务器监听socket用）
Socket(int fd)  → 用已有fd构造（accept返回的客户端fd用）
```

accept 返回的是裸 fd，用 `Socket(int fd)` 包装后就能享受 RAII 的自动 close。

### 全局函数 vs 类方法

本阶段 `handle_read()` 仍是全局函数，接收裸 fd。业务逻辑还没抽象进类——这是 5_Channel/6_Server 才做的事。本阶段只完成了**资源封装**，还没做**事件抽象**。

---

## 六、完整生命周期

```
出生：服务器启动
  1. new Socket() → socket() 创建监听fd
  2. bind + listen
  3. new Epoll() → epoll_create1()
  4. epoll->add(监听fd) → 注册到epoll

工作：事件循环
  1. epoll->poll() → epoll_wait 阻塞等事件
  2. 监听fd可读 → accept → new Socket(client_fd) → epoll->add(客户端fd)
  3. 客户端fd可读 → handle_read → while(read) 回显 → EAGAIN break

死亡：客户端断开
  1. read 返回 0
  2. close(fd)  ← 注意：这里没有 delete Socket 对象，存在泄漏
```

> 本阶段 `new Socket()` 和 `new Epoll()` 都没有 `delete`，存在内存泄漏。学习示例可接受，后续阶段会修复。

---

## 七、3→4 演进总结

| 维度 | 3_Epoll | 4_WrapperClass |
|------|---------|----------------|
| 代码组织 | 全在 main()，裸 API | ✅ 拆出 Socket/InetAddress/Epoll 类 |
| 资源管理 | 手动 close fd | ✅ RAII 自动管理 |
| 地址设置 | memset + 填三字段 | ✅ InetAddress 构造函数一行搞定 |
| epoll 注册 | epoll_ctl(ADD, fd) | ✅ epoll->add(fd, events) |
| 事件返回 | vector<epoll_event> | 同（仍返回 epoll_event，未变） |
| 业务逻辑 | 全局函数 handle_read | 同（仍是全局函数，未抽象） |

---

## 八、编译与运行

```bash
cd 4_WrapperClass
make

# 终端 1：启动服务器
./server

# 终端 2：启动客户端，输入文字回车发送
./client
> hello server
```

---

## 九、完整演进脉络

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简 socket：accept 一个客户端就退出 |
| 2_EchoAndUtil | 加 read/write 循环 + errif 错误处理 |
| 3_Epoll | 引入 epoll 多路复用，支持多客户端 |
| **4_WrapperClass** | **封装 Socket/InetAddress/Epoll 类（RAII）** |
| 5_Channel | 引入 Channel 抽象：fd+events+revents 绑定 |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> Socket/InetAddress/Epoll 这三个类被后续所有阶段直接沿用。到 5_Channel 时 Epoll 会新增 `updateChannel` 方法（用 `data.ptr` 替代 `data.fd`），到 6_Server 时事件循环会被抽进 EventLoop 类。
