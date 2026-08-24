# 5_Channel — fd + 状态抽象

## 这版解决什么问题

让 epoll 返回 `Channel*` 而不是裸的 fd，**不再需要 if/else 判断 fd 类型**。

## 引入 Channel 类

Channel = 一个 fd 的「个人信息卡」，记录 fd、监听的事件、实际发生的事件。

```cpp
class Channel {
private:
    Epoll* m_epoll;        // 属于哪个 Epoll
    int m_fd;              // 绑定的 fd
    uint32_t m_events;     // 要监听的事件（EPOLLIN 等）
    uint32_t m_revents;    // 实际发生的事件（epoll_wait 返回时填）
    bool m_inEpoll;        // 是否已加入 epoll
};
```

## 关键技巧：Epoll 里存 Channel 指针

在 `Epoll::updateChannel` 中：
```cpp
ev.data.ptr = channel;    // 把 Channel* 存进 epoll_event
```

在 `Epoll::poll` 中：
```cpp
Channel* channel = static_cast<Channel*>(m_events[i].data.ptr);
channel->SetRevents(m_events[i].events);
activeChannels.push_back(channel);
```

**epoll_wait 返回时直接拿到 Channel 对象**，不用查表判断 fd 类型。

## 对比：第 4 版 vs 第 5 版

**第 4 版**：
```cpp
std::vector<epoll_event> events = epoll->poll();
// events[i].data.fd 只是个 int
// events[i].events 是事件类型
// 需要自己判断：if(是监听 fd) ... else if(是可读) ...
```

**第 5 版**：
```cpp
std::vector<Channel*> activeChannel = epoll->poll();
// activeChannel[i] 直接是 Channel* 对象
// Channel 知道自己的 fd、状态
```

## 启动时的流程

```cpp
// 为监听 socket 建 Channel
Channel* serverChannel = new Channel(epoll, serverSocket->getSockfd());
serverChannel->enableReading();   // 内部调 epoll->updateChannel → epoll_ctl ADD

// 为新客户端建 Channel
Channel* clientChannel = new Channel(epoll, clientFd);
clientChannel->enableReading();   // 同样加入 epoll
```

## 与上一版的差异

| | 4_WrapperClass | 5_Channel |
|---|---------------|-----------|
| epoll 返回类型 | `epoll_event` 数组 | `Channel*` 数组 |
| fd 管理 | 裸 int | Channel 对象封装 |
| 上下文信息 | 只有 fd | fd + events + revents |
| **核心逻辑** | 不变 | 不变 |

## 这版的限制

- Channel **没有回调**——它只知道 fd 和状态，不知道「事件来了该怎么处理」
- 业务逻辑仍在 main 里的 if/else 分支
- 每个 fd 的处理逻辑还是写死

## 与下一版的衔接

第 6 版将给 Channel 加上「回调」成员，引入 `EventLoop`（事件循环骨架）和 `Server`（业务入口），main 函数从此不再处理业务。
