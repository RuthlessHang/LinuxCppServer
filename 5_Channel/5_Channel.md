# 5_Channel 逻辑总结

## 一、类与类的包含关系

本阶段引入 **Channel 类**——Reactor 模式的奠基抽象。Channel 把"一个 fd + 关心的事件 + 实际发生的事件 + 是否已注册"绑成一个对象，并通过 `ev.data.ptr = channel` 让 epoll_wait 直接返回 Channel 指针。

```
main()
  ├── Socket* serverSocket            （监听fd的RAII封装）
  ├── InetAddress serverAddr          （地址封装）
  ├── Epoll* epoll                    （epoll实例）
  │     └── epoll_event[].data.ptr    ← ★存的是 Channel* 指针（不是fd了）
  │
  ├── Channel* serverChannel          （★新增：监听fd的事件抽象）
  │     ├── m_fd = 监听fd
  │     ├── m_events = EPOLLIN|EPOLLET
  │     ├── m_revents = 0（等内核回填）
  │     └── m_inEpoll = true（注册后设为true）
  │
  └── while(true) 事件循环
        ├── epoll->poll()             → ★返回 vector<Channel*>（不再是epoll_event）
        └── for 每个Channel*：
              ├── channel->Getfd() == 监听fd？
              │   → accept + new Channel(客户端fd) + enableReading
              └── channel->GetRevents() & EPOLLIN？
                  → handle_read(channel->Getfd())
```

### 每个类的职责

| 类 | 职责 | 存了什么 | 本阶段变化 |
|----|------|---------|-----------|
| `Socket` | fd 的 RAII 封装 | m_sockfd | 不变 |
| `InetAddress` | 地址封装 | m_addr, m_addr_len | 不变 |
| `Epoll` | epoll 封装 | m_epoll_fd, m_events[] | ★新增 updateChannel，poll 返回 Channel* |
| `Channel` | ★新增：fd 的事件抽象 | m_fd, m_events, m_revents, m_inEpoll | 本阶段核心 |
| `util` | 错误处理 | — | 不变 |

### Channel 的四要素

Channel 把分散的信息聚合成一个对象：

| 成员 | 类型 | 含义 | 阶段 4 时这些信息在哪 |
|------|------|------|---------------------|
| `m_fd` | int | 绑定的文件描述符 | epoll_event.data.fd（散落） |
| `m_events` | uint32_t | 用户关心的事件 | 调用点硬编码 EPOLLIN\|EPOLLET |
| `m_revents` | uint32_t | 内核实际返回的事件 | epoll_event.events（散落） |
| `m_inEpoll` | bool | 是否已注册到 epoll | 没地方记（每次都 add） |

---

## 二、核心设计：data.ptr 技巧

这是本阶段最重要的架构设计，也是 Reactor 模式的核心技巧。

### 阶段 4 的方式（data.fd）

```
注册时：ev.data.fd = sockfd          ← 存整数
返回时：events[i].data.fd = 某个整数  ← 拿到整数
        → 需要逐个比较：if(fd == 监听fd) ...  ← O(n) 比较，麻烦
```

### 阶段 5 的方式（data.ptr）

```
注册时：ev.data.ptr = channel        ← 存 Channel 指针
返回时：events[i].data.ptr = Channel* ← 直接拿到 Channel 对象
        → 无需比较，直接用            ← O(1) 访问，清爽
```

`epoll_event.data` 是一个联合体，可以存 `fd`、`ptr`、`u32` 等。存 Channel 指针后，epoll_wait 返回时内核把指针原样返回，省掉 fd→Channel 的映射查找。这是 muduo 等高性能网络库的通用做法。

---

## 三、启动流程

### 第 1 步：创建监听 Socket（同阶段 4）

```cpp
Socket* serverSocket = new Socket();
InetAddress serverAddr = InetAddress("192.168.48.128", 8080);
serverSocket->bind(serverAddr);
serverSocket->listen();
```

### 第 2 步：创建 Epoll 和监听 Channel

```cpp
Epoll* epoll = new Epoll();
serverSocket->setNonBlocking();

// ★本阶段新增：为监听fd创建Channel
Channel* serverChannel = new Channel(epoll, serverSocket->GetSockfd());
serverChannel->enableReading();   // 一行完成：设置事件 + 注册到epoll
```

`enableReading()` 内部做了两件事：

```
Channel::enableReading()
  ├─ m_events = EPOLLIN | EPOLLET       ← 设置关心的事件
  └─ m_epoll->updateChannel(this)       ← 把自己注册到 epoll
       └─ epoll_ctl(ADD/MOD, fd, ev)
            └─ ev.data.ptr = this       ← ★存Channel指针
```

对比阶段 4：

```cpp
// 阶段 4：事件类型硬编码在调用点
epoll->add(serverSocket->GetSockfd(), EPOLLIN | EPOLLET);

// 阶段 5：事件封装在Channel内部
serverChannel->enableReading();
```

### 第 3 步：事件循环

```cpp
while(true)
{
    std::vector<Channel*> activeChannel = epoll->poll();  // ★返回Channel*
    for(int i = 0; i < activeChannel.size(); i++)
    {
        if(activeChannel[i]->Getfd() == serverSocket->GetSockfd())
            // 监听Channel → accept
        else if(activeChannel[i]->GetRevents() & EPOLLIN)
            // 客户端Channel → handle_read
    }
}
```

---

## 四、Epoll 的关键变更

### 变更 1：新增 updateChannel（替代 add）

```cpp
void Epoll::updateChannel(Channel* channel)
{
    ev.events = channel->GetEvents();
    ev.data.ptr = channel;    // ★存Channel指针

    if(channel->GetInEpoll())                          // 已注册 → MOD
        epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    else                                                // 未注册 → ADD
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}
```

调用方只需 `enableReading()`，不必关心是 ADD 还是 MOD——这由 `m_inEpoll` 标志自动判断。

### 变更 2：poll 返回 vector\<Channel*\>

```cpp
std::vector<Channel*> Epoll::poll(int timeout)
{
    int nfds = epoll_wait(m_epoll_fd, m_events, MAX_EVENTS, timeout);
    for(int i = 0; i < nfds; i++) {
        Channel* channel = static_cast<Channel*>(m_events[i].data.ptr); // 取回指针
        channel->SetRevents(m_events[i].events);                         // 回填实际事件
        activeChannels.push_back(channel);
    }
    return activeChannels;
}
```

poll 时做三件事：取回 Channel 指针 → 回填 revents → 返回 Channel 数组。主循环拿到的直接就是 Channel 对象。

---

## 五、运行时流程

### 新客户端连接

```
客户端 connect()
  ↓
内核：监听fd可读
  ↓
epoll->poll() 返回 serverChannel
  ↓
while(true) {
  InetAddress clientAddr;
  int clientFd = serverSocket->accept(clientAddr);
  if(clientFd == -1 && errno == EAGAIN) break;    ← ET 接完所有连接
  setNonBlocking(clientFd);
  Channel* clientChannel = new Channel(epoll, clientFd);  ← ★为客户端创建Channel
  clientChannel->enableReading();                          ← 注册到epoll
}
```

### 客户端发数据

```
客户端 write("hello")
  ↓
内核：客户端fd可读
  ↓
epoll->poll() 返回 clientChannel（通过data.ptr直接拿到）
  ↓
channel->GetRevents() & EPOLLIN  ← 读实际事件
  ↓
handle_read(channel->Getfd())     ← 仍是全局函数
  └─ while(read): 读到"hello" → 打印 → EAGAIN → break
```

---

## 六、完整生命周期

```
出生：服务器启动
  1. new Socket() → bind + listen
  2. new Epoll() → epoll_create1()
  3. new Channel(epoll, 监听fd) → enableReading() → epoll_ctl(ADD)

工作：新客户端连接
  1. epoll->poll() 返回 serverChannel
  2. accept → clientFd
  3. new Channel(epoll, clientFd) → enableReading() → epoll_ctl(ADD)
  // epoll里现在有多个Channel，每个Channel封装一个fd

工作：客户端发数据
  1. epoll->poll() 返回 clientChannel
  2. handle_read(channel->Getfd()) → while(read) 回显

死亡：客户端断开
  1. read 返回 0 → close(fd)
  // Channel对象未delete（泄漏，后续阶段修复）
```

---

## 七、设计理念

### Channel 解决了什么问题

阶段 4 的痛点：fd、关心的事件、是否已注册——这些信息**散落各处**，每来新连接都要手动 `epoll->add(fd, EPOLLIN|EPOLLET)`。

```
阶段 4（分散）：                      阶段 5（聚合）：
  fd 在 epoll_event.data.fd            Channel {
  事件硬编码在调用点                      m_fd,        ← fd
  是否已注册？没地方记                     m_events,    ← 关心的事件
                                          m_revents,   ← 实际事件
                                          m_inEpoll    ← 是否已注册
                                        }
```

### enableReading 的语义

`enableReading()` = "我想监听这个 fd 的可读事件"。调用方不用关心 `EPOLLIN|EPOLLET` 常量，也不用关心是 ADD 还是 MOD。后续可对称加 `enableWriting()`。

### 为什么还没有回调

本阶段 Channel 有 fd/events/revents/inEpoll，**唯独缺回调**。主循环仍靠 `if/else` 判断 fd 类型来分发：

```cpp
if(channel->Getfd() == 监听fd) { /* accept */ }
else if(channel->GetRevents() & EPOLLIN) { /* handle_read */ }
```

当 fd 种类变多时，这种 if/else 会膨胀成噩梦。6_Server 会给 Channel 加 `m_Callback`，事件触发时直接 `channel->handleEvent()` 调回调，彻底消灭 if/else 分发。

---

## 八、4→5 演进总结

| 维度 | 4_WrapperClass | 5_Channel |
|------|----------------|-----------|
| poll 返回类型 | `vector<epoll_event>` | `vector<Channel*>` |
| epoll_event.data | `data.fd`（存整数） | `data.ptr`（存Channel指针） |
| 区分 fd 方式 | `events[i].data.fd == 监听fd` 比较 | `channel->Getfd() == 监听fd` |
| 注册新 fd | `epoll->add(fd, EPOLLIN\|EPOLLET)` | `channel->enableReading()` |
| 事件类型管理 | 调用点硬编码 | Channel 内部统一管理 |
| ADD/MOD 判断 | 无（只有 add） | updateChannel 根据 inEpoll 自动选 |
| 回调机制 | ❌ 无 | ❌ 无（下阶段才加） |

---

## 九、编译与运行

```bash
cd 5_Channel
make

# 终端 1：启动服务器
./server

# 终端 2：启动客户端
./client
> hello server
```

---

## 十、完整演进脉络

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简 socket：accept 一个客户端就退出 |
| 2_EchoAndUtil | 加 read/write 循环 + errif 错误处理 |
| 3_Epoll | 引入 epoll 多路复用，支持多客户端 |
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| **5_Channel** | **引入 Channel 抽象：fd+events 绑定，data.ptr 技巧** |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> Channel 类被后续所有阶段直接沿用。到 6_Server 时，Channel 会新增 `m_Callback`（std::function）和 `handleEvent()` 方法，彻底消灭主循环的 if/else 分发。到 8_TCP 时，Channel 会把 `m_epoll` 改为 `m_loop`（EventLoop*），通过 EventLoop 间接注册到 epoll。
