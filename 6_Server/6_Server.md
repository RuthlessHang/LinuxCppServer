# 6_Server 逻辑总结

## 一、类与类的包含关系

本阶段是 **Reactor 模式正式成型**的里程碑。引入 EventLoop（事件循环）和 Server（业务管理），并给 Channel 加了回调函数。从此主循环不再用 if/else 分发，而是通过回调自动触发。

```
main.cpp
  ├── EventLoop* loop              （事件循环：发动机）
  │     └── Epoll* m_epoll         （事件分发：管理所有fd）
  │           └── epoll里注册了多个Channel*（通过data.ptr存储）
  │
  └── Server* server               （业务管理：管监听和读写）
        ├── Socket* serverSocket   （监听fd）
        ├── Channel* serverChannel （监听Channel，回调=newConnection）
        │
        └── 每个客户端连接（本阶段未用map管理，散落各处）
              ├── Socket* clientSocket   （客户端fd）
              └── Channel* clientChannel （客户端Channel，回调=handleReadEvent）
```

### 每个类的职责

| 类 | 职责 | 存了什么 | 本阶段变化 |
|----|------|---------|-----------|
| `EventLoop` | ★新增：死循环等事件，分发给Channel | Epoll*, quit | 本阶段核心 |
| `Server` | ★新增：业务管理，接受连接+处理读 | EventLoop*, Acceptor* | 本阶段核心 |
| `Channel` | fd的事件抽象 | fd, events, revents, inEpoll, **m_Callback** | ★新增回调 |
| `Epoll` | epoll封装 | epoll_fd, events[] | 不变 |
| `Socket` | fd的RAII封装 | m_sockfd | 不变 |
| `InetAddress` | 地址封装 | m_addr, m_addr_len | 不变 |

### 生命周期管理

```
main 拥有：
  ├── EventLoop*    → delete in ~main()（本阶段未delete，泄漏）
  └── Server*       → delete in ~main()（本阶段未delete，泄漏）

Server 拥有（本阶段）：
  ├── Socket* serverSocket   → 未delete（泄漏）
  └── 每个客户端的 Socket* + Channel* → 未delete（泄漏）

EventLoop 拥有：
  └── Epoll* m_epoll  → delete in ~EventLoop() ✅
```

> 本阶段存在内存泄漏（new 的对象未 delete），7_Acceptor 和 8_TCP 会逐步修复。

---

## 二、启动流程

### 第 1 步：main.cpp

```cpp
EventLoop* loop = new EventLoop();   // 创建事件循环，内部 new Epoll()
Server* server = new Server(loop);   // 创建服务器，传入loop
loop->loop();                        // 进入死循环，等待事件
```

### 第 2 步：EventLoop 构造

```cpp
EventLoop::EventLoop() {
    m_epoll = new Epoll();    // 创建Epoll，内部 epoll_create1()
}
```

### 第 3 步：Server 构造（完成服务端监听的所有初始化）

```cpp
Server::Server(EventLoop *loop) : m_loop(loop) {
    Socket* serverSocket = new Socket();                    // 1. 创建监听fd
    InetAddress serverAddr("192.168.48.128", 8888);
    serverSocket->bind(serverAddr);                         // 2. 绑定
    serverSocket->listen();                                 // 3. 监听
    serverSocket->setNonBlocking();                         // 4. 非阻塞（ET必须）

    Channel* serverChannel = new Channel(m_loop, serverSocket->getSockfd()); // 5. 创建Channel
    // 6. 存回调：告诉Channel"监听fd有事件时调用我的newConnection"
    std::function<void()> callback = std::bind(&Server::newConnection, this, serverSocket);
    serverChannel->setCallback(callback);
    serverChannel->enableReading();                         // 7. 注册到epoll
}
```

**启动完成后的内存状态**：

```
epoll里只有1个fd：监听fd
Channel(监听fd).m_Callback = newConnection（绑定了serverSocket参数）
```

然后 `loop->loop()` 开始死循环：

```cpp
void EventLoop::loop() {
    while (!quit) {
        vector<Channel*> active = m_epoll->poll();   // epoll_wait 阻塞等事件
        for (Channel* ch : active) {
            ch->handleEvent();                        // 调用Channel存的回调
        }
    }
}
```

**这就是经典 Reactor 循环：等待事件 → 分发回调 → 等待下一批。**

---

## 三、两条回调链（运行时核心）

本阶段最关键的设计：用 `std::function` + `std::bind` 把"事件触发"和"处理逻辑"解耦。

### 回调链全景

```
链① Channel.m_Callback = newConnection     （监听fd有事件时）
链② Channel.m_Callback = handleReadEvent   （客户端fd有事件时）
```

### 新客户端连接（链①）

```
客户端 connect()
  ↓
内核：监听fd可读
  ↓
epoll_wait 返回监听Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链①：Server::newConnection(serverSocket)
```

### newConnection 逐行解析

```cpp
void Server::newConnection(Socket *serverSocket) {
    InetAddress clientAddr;
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));  // 1. accept
    if(clientSocket->getSockfd() != -1) {
        clientSocket->setNonBlocking();                                    // 2. 非阻塞
        Channel* clientChannel = new Channel(m_loop, clientSocket->getSockfd()); // 3. 创建Channel
        // 4. 存回调②：告诉Channel"客户端fd有事件时调用我的handleReadEvent"
        clientChannel->setCallback(std::bind(&Server::handleReadEvent, this, clientChannel->getFd()));
        clientChannel->enableReading();                                    // 5. 注册到epoll
    }
}
```

**newConnection 执行完后**：epoll 里现在有 2 个 fd（监听fd + 客户端fd），各有自己的 Channel 和回调。

### 客户端发数据（链②）

```
客户端 write("hello")
  ↓
内核：客户端fd可读
  ↓
epoll_wait 返回客户端Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链②：Server::handleReadEvent(fd)
    → while(read) → 打印 "hello" → EAGAIN → break
```

**关键变化**：相比阶段 5 的 `if/else` 分发，本阶段 Channel 直接调回调，**不再需要判断 fd 类型**。

### 两条链对比

| 链 | 存储位置 | 触发时机 | 调用谁 |
|----|---------|---------|--------|
| ① | Channel.m_Callback | 监听fd可读 | Server::newConnection() |
| ② | Channel.m_Callback | 客户端fd可读 | Server::handleReadEvent() |

**链①②都是 Channel.m_Callback，怎么区分？** 它们是不同 Channel 对象的回调：
- 监听 Channel → 回调① = newConnection
- 客户端 Channel → 回调② = handleReadEvent

---

## 四、std::bind 的作用

本阶段第一次使用回调绑定，这是理解后续阶段（7_Acceptor、8_TCP）回调链的基础。

### bind 用法①：绑定 newConnection（有参数）

```cpp
std::bind(&Server::newConnection, this, serverSocket)
// 函数类型：void()  ← 无参数（serverSocket在绑定时就固定了）
// 调用时：callback()  ← 不需要传参
```

成员函数需要 `this` 指针，`serverSocket` 在绑定时就传入，所以回调类型是 `void()`。

### bind 用法②：绑定 handleReadEvent（有参数）

```cpp
std::bind(&Server::handleReadEvent, this, clientChannel->getFd())
// 函数类型：void()  ← 无参数（fd在绑定时就固定了）
// 调用时：callback()  ← 不需要传参
```

fd 在创建 Channel 时就确定了，绑定时直接传入。这样 Channel 的 `m_Callback` 统一是 `void()` 类型，调用时只需 `m_Callback()` 即可。

---

## 五、完整生命周期

```
出生：客户端 connect
  1. epoll_wait → 监听Channel → 链① newConnection
  2. accept() → 得到 client_fd
  3. new Channel → 存回调② handleReadEvent + enableReading(注册到epoll)

工作：客户端发数据
  1. epoll_wait → 客户端Channel → 链② handleReadEvent
  2. while(read) → 打印数据 → EAGAIN → break

死亡：客户端 close
  1. epoll_wait → 客户端Channel → 链② handleReadEvent
  2. read() 返回 0 → close(fd)
  // Channel/Socket对象未delete（泄漏，8_TCP修复）
```

---

## 六、设计理念

### 为什么这样分层

```
EventLoop + Epoll + Channel  = 可复用的Reactor框架（不知道业务）
Server                       = 业务模块（知道具体做什么）
```

**EventLoop 不知道 Server 的存在**——它只管从 epoll 拿到就绪 Channel，调 `handleEvent()`。Channel 内部的回调是什么，EventLoop 不关心。

**以后要改成HTTP服务器**：只需把 Server 里的 `handleReadEvent` 从 echo 改成 HTTP 解析，EventLoop/Channel/Epoll 不用动。

### 回调代替 if/else

```
阶段 5（if/else 分发）：              阶段 6（回调分发）：
  if(fd == 监听fd) accept             channel->handleEvent()
  else handle_read(fd)                  → m_Callback()
                                       → 自动调到正确的函数
  fd类型多了就膨胀                     fd类型多少都不影响
```

### EventLoop 的角色

EventLoop 是"发动机"，唯一的 `while(true)` 在这里。它把 Channel 和 Epoll 解耦：

```
Channel → m_loop->updateChannel(this) → EventLoop → m_epoll->updateChannel(this)
```

Channel 不直接依赖 Epoll，而是通过 EventLoop 间接注册。这样以后 EventLoop 可以加定时器、信号处理等功能，不影响 Channel。

---

## 七、5→6 演进总结

| 维度 | 5_Channel | 6_Server |
|------|-----------|----------|
| 事件循环 | main 里的 while(true) | ✅ EventLoop 类封装 |
| 业务逻辑 | main + 全局函数 handle_read | ✅ Server 类封装 |
| 事件分发 | if/else 判断 fd 类型 | ✅ 回调自动触发 |
| Channel 回调 | ❌ 无 | ✅ m_Callback + handleEvent() |
| 启动方式 | main 干所有事 | ✅ main 只有三行 |

---

## 八、编译与运行

```bash
cd 6_Server
make

# 终端 1：启动服务器
./server

# 终端 2：启动客户端
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
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| 5_Channel | 引入 Channel 抽象：fd+events 绑定 |
| **6_Server** | **引入 EventLoop+Server+回调，Reactor 成型** |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> 6_Server 的架构是后续阶段的基石。7_Acceptor 会把 Server 构造里的"监听逻辑"拆进 Acceptor 类，8_TCP 会把"连接读写逻辑"拆进 Connection 类，Server 逐渐变成只管生死的纯管理者。
