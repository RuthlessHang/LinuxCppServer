# 7_Acceptor 逻辑总结

## 一、类与类的包含关系

相比 6_Server，本阶段把 Server 构造里的"**监听 Socket + bind + listen + 注册 Channel**"逻辑拆进了独立的 **Acceptor 类**。Server 不再直接碰 Socket 和 Channel，而是通过 Acceptor 间接管理监听。

```
main.cpp
  ├── EventLoop* loop              （事件循环：发动机）
  │     └── Epoll* m_epoll         （事件分发：管理所有fd）
  │           └── epoll里注册了多个Channel*（通过data.ptr存储）
  │
  └── Server* server               （业务管理：只管Acceptor和连接的生死）
        ├── Acceptor* m_acceptor          ★新增（监听：站岗放哨）
        │     ├── Socket* m_Socket        （监听fd）
        │     ├── InetAddress* m_addr     （服务端地址）
        │     └── Channel* m_acceptChannel（监听Channel，回调=acceptConnection）
        │
        └── 每个客户端连接（散落各处，8_TCP才用map管理）
              ├── Socket* clientSocket   （客户端fd）
              └── Channel* clientChannel （客户端Channel，回调=handleReadEvent）
```

### 6→7 的架构变化

```
6_Server 的 Server 构造：               7_Acceptor 的 Server 构造：
  Server                                Server
  ├── Socket* serverSocket     ──→     └── Acceptor* m_acceptor
  ├── InetAddress                       ├── Socket* m_Socket     ← 全搬到Acceptor
  ├── bind / listen / setNonBlocking    ├── InetAddress* m_addr
  ├── Channel* serverChannel            ├── bind / listen / setNonBlocking
  └── setCallback(newConnection)        ├── Channel* m_acceptChannel
                                        └── setCallback(acceptConnection)
                                        
                                        + Server 存回调到 Acceptor:
                                          setNewConnectionCallback(newConnection)
```

**一句话**：Server 把"站岗放哨（监听）"外包给了 Acceptor，Acceptor 发现有新客户端时**通过回调通知**Server 来接客。

### 每个类的职责

| 类 | 职责 | 存了什么 | 本阶段变化 |
|----|------|---------|-----------|
| `Acceptor` | ★新增：监听Socket的初始化 + 通知Server有新连接 | Socket*, InetAddress*, Channel*, callback | 本阶段核心 |
| `Server` | 业务管理：接受连接 + 处理读 | EventLoop*, Acceptor* | ★去掉监听逻辑，改为持有Acceptor |
| `EventLoop` | 死循环等事件 | Epoll*, quit | 不变 |
| `Channel` | fd的事件抽象 | fd, events, revents, inEpoll, m_Callback | 不变 |
| `Epoll` / `Socket` / `InetAddress` | 底层封装 | — | 不变 |

### 生命周期管理

```
Server 拥有并管理：
  └── Acceptor* m_acceptor  → delete in ~Server()  ✅（6_Server没有这一步）

Acceptor 拥有并管理：
  ├── Socket* m_Socket        → delete in ~Acceptor()  ✅
  ├── InetAddress* m_addr     → delete in ~Acceptor()  ✅
  └── Channel* m_acceptChannel → delete in ~Acceptor() ✅
```

**规则**：谁 new 的谁 delete。Server new 了 Acceptor，所以 Server 析构删它。Acceptor new 了自己的 Socket/Channel，所以 Acceptor 析构删它们。相比 6_Server 的全部泄漏，本阶段在 Acceptor 层面实现了内存管理。

---

## 二、启动流程

### 第 1 步：main.cpp（不变）

```cpp
EventLoop* loop = new EventLoop();   // 创建事件循环
Server* server = new Server(loop);   // 创建服务器
loop->loop();                        // 进入死循环
```

### 第 2 步：Server 构造（变了！）

```cpp
Server::Server(EventLoop *loop) : m_loop(loop), m_acceptor(nullptr) {
    m_acceptor = new Acceptor(loop);    // 1. 把监听工作外包给Acceptor
    // 2. 存回调②：告诉Acceptor"有新连接时调用我的newConnection"
    std::function<void(Socket*)> callback =
        std::bind(&Server::newConnection, this, std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback);
}
```

对比 6_Server 的 Server 构造（12行 → 4行）：

```cpp
// 6_Server：Server 自己干所有事
Socket* serverSocket = new Socket();
InetAddress serverAddr = InetAddress("192.168.48.128", 8888);
serverSocket->bind(serverAddr);
serverSocket->listen();
serverSocket->setNonBlocking();
Channel* serverChannel = new Channel(m_loop, serverSocket->getSockfd());
serverChannel->setCallback(std::bind(&Server::newConnection, this, serverSocket));
serverChannel->enableReading();

// 7_Acceptor：Server 只管 Acceptor + 注册回调
m_acceptor = new Acceptor(loop);
m_acceptor->setNewConnectionCallback(callback);
```

### 第 3 步：Acceptor 构造（完成服务端监听的所有初始化）

```cpp
Acceptor::Acceptor(EventLoop* loop) {
    m_Socket = new Socket();                              // 1. 创建监听fd
    m_addr = new InetAddress("192.168.48.128", 8888);     // 2. 设置地址
    m_Socket->bind(*m_addr);                              // 3. 绑定
    m_Socket->listen();                                   // 4. 监听
    m_Socket->setNonBlocking();                           // 5. 非阻塞（ET必须）

    m_acceptChannel = new Channel(m_loop, m_Socket->getSockfd());          // 6. 创建Channel
    m_acceptChannel->setCallback(std::bind(&Acceptor::acceptConnection, this)); // 7. 存回调①
    m_acceptChannel->enableReading();                     // 8. 注册到epoll
}
```

**启动完成后的内存状态**：

```
epoll里只有1个fd：监听fd
Channel(监听fd).m_Callback = acceptConnection        ← 回调①
Acceptor.m_newConnectionCallback = newConnection     ← 回调②（Server构造时存的）
```

---

## 三、双层回调链（运行时核心）

这是本阶段最重要的架构设计——两条回调链串联了"事件触发 → 业务处理"的全过程。

### 回调链全景

```
链① Channel.m_Callback = acceptConnection          （监听fd有事件时）
链② Acceptor.m_newConnectionCallback = newConnection（acceptConnection内部转调）
```

### 新客户端连接（链①②）

```
客户端 connect()
  ↓
内核：监听fd可读
  ↓
epoll_wait 返回监听Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链①：Acceptor::acceptConnection()
    → 里面只有一行：m_newConnectionCallback(m_Socket)
      → 链②：Server::newConnection(监听Socket)
```

### acceptConnection 只做一件事

```cpp
void Acceptor::acceptConnection() {
    m_newConnectionCallback(m_Socket);   // 转调Server的回调，把监听Socket传过去
}
```

**Acceptor 是中间的转发层**——Channel 触发它，它再触发 Server。

### newConnection 逐行解析（与 6_Server 相同）

```cpp
void Server::newConnection(Socket *serverSocket) {
    InetAddress clientAddr;
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));  // accept
    if(clientSocket->getSockfd() != -1) {
        clientSocket->setNonBlocking();
        Channel* clientChannel = new Channel(m_loop, clientSocket->getSockfd());
        clientChannel->setCallback(std::bind(&Server::handleReadEvent, this, clientChannel->getFd()));
        clientChannel->enableReading();    // 注册到epoll
    }
}
```

### 客户端发数据（与 6_Server 相同）

```
客户端 write("hello")
  ↓
epoll_wait 返回客户端Channel
  ↓
Channel::handleEvent() → m_Callback()
  → Server::handleReadEvent(fd)
    → while(read) → 打印 → EAGAIN → break
```

这一步不经过 Acceptor，与 6_Server 完全相同。

### 两条链对比

| 链 | 存储位置 | 触发时机 | 调用谁 |
|----|---------|---------|--------|
| ① | Channel.m_Callback | 监听fd可读 | Acceptor::acceptConnection() |
| ② | Acceptor.m_newConnectionCallback | acceptConnection内部 | Server::newConnection() |

---

## 四、为什么需要两条链

如果把 Server 的 newConnection 直接传给 Channel，确实只要一条。但这样 Channel 就直接认识 Server 了，耦合太紧：

```
一条链（耦合）：                   两条链（解耦）：
  Channel → Server::newConnection    Channel → Acceptor → Server
  
  Channel 认识 Server                Channel 只认识 Acceptor（通用）
  换业务就得改 Channel                Acceptor 只认识 Server（业务）
                                     换业务只改 Server，Acceptor/Channel 不动
```

```
Channel 只认识 Acceptor（通用，任何服务器都能用）
Acceptor 只认识 Server（具体业务）
```

**Acceptor 是中间的解耦层**——以后换业务逻辑（比如改成聊天室、HTTP服务器），只需要改 Server 里的 newConnection，Acceptor、Channel、EventLoop **一行都不用改**。

---

## 五、std::placeholders::_1 的区别

本阶段用了两种不同的 bind 方式：

### bind 用法①：Acceptor 传给 Channel（无参数）

```cpp
std::bind(&Acceptor::acceptConnection, this)
// 函数类型：void()  ← 无参数
// 绑定时所有参数都确定了
```

Channel 回调时只要调 `callback()` 就行。

### bind 用法②：Server 传给 Acceptor（留1个参数）

```cpp
std::bind(&Server::newConnection, this, std::placeholders::_1)
// 函数类型：void(Socket*)  ← 有1个参数
// _1 表示：这个参数调用时才传入
```

Acceptor 触发时要调 `callback(m_Socket)`，把自己的监听 Socket 传过去，因为 Server 的 accept() 需要用到它。

---

## 六、完整生命周期

```
出生：客户端 connect
  1. epoll_wait → 监听Channel → 链① acceptConnection → 链② newConnection
  2. accept() → 得到 client_fd
  3. new Channel → 存回调 handleReadEvent + enableReading(注册到epoll)

工作：客户端发数据
  1. epoll_wait → 客户端Channel → handleReadEvent
  2. while(read) → 打印数据 → EAGAIN → break

死亡：客户端 close
  1. epoll_wait → 客户端Channel → handleReadEvent
  2. read() 返回 0 → close(fd)
  // 客户端Channel/Socket未delete（8_TCP才修复）
```

---

## 七、设计理念

### 单一职责

```
6_Server：Server 既监听又处理连接又处理读写（什么都干）
7_Acceptor：Server 只管业务，Acceptor 只管监听（各司其职）
```

### 开闭原则

新增 Acceptor 类时，EventLoop / Channel / Epoll / Socket / InetAddress **一行都没改**。只新增了 2 个文件（Acceptor.hpp/cpp）+ 修改了 2 个文件（Server.hpp/cpp）。这就是模块化的好处。

### 回调代替继承

没有用继承+虚函数（class Acceptor : public SomeBase），而是用 `std::function` + `std::bind` 注册回调。代码更直观，不需要为每种回调创建子类。

### 功能没变却要重构

本阶段功能上和 6_Server **没有任何变化**（客户端发消息服务器打印）。改的是内部结构——结构变优雅了，以后加功能（多线程、HTTP、聊天室）会简单很多。

---

## 八、6→7 演进总结

| 维度 | 6_Server | 7_Acceptor |
|------|----------|------------|
| 监听逻辑 | Server 构造里直接写 | ✅ 拆进 Acceptor 类 |
| Server 构造行数 | 12行 | 4行 |
| 内存管理 | 全部泄漏 | ✅ Acceptor 析构删自己的资源 |
| 回调链 | 1条（Channel→Server） | ✅ 2条（Channel→Acceptor→Server） |
| 底层类改动 | — | 0行（EventLoop/Channel/Epoll不变） |

---

## 九、编译与运行

与 6_Server 完全相同，Makefile 用的 `src/*.cpp` 会自动包含新增的 Acceptor.cpp：

```bash
cd 7_Acceptor
make clean && make

./server     # 终端1
./client     # 终端2，发送消息
```

功能上和 6_Server **没有任何变化**——改的是内部结构，不是功能。

---

## 十、完整演进脉络

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简 socket：accept 一个客户端就退出 |
| 2_EchoAndUtil | 加 read/write 循环 + errif 错误处理 |
| 3_Epoll | 引入 epoll 多路复用，支持多客户端 |
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| 5_Channel | 引入 Channel 抽象：fd+events 绑定 |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| **7_Acceptor** | **拆出 Acceptor，监听逻辑独立，双层回调链** |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> 7_Acceptor 的双层回调链设计是 8_TCP 四条回调链的基础。8_TCP 会在此基础上再加两条链：客户端Channel的回调③（handleReadEvent）和Connection的回调④（deleteConnection），形成完整的"从生到死"回调网络。
