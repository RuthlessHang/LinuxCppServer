# 3_Epoll — I/O 多路复用

## 这版解决什么问题

服务器能**同时服务多个客户端**，而不是一次只能服务一个。核心是引入 epoll 实现 I/O 复用。

## I/O 复用是什么

**一个线程同时监听多个 I/O 事件，谁有事就处理谁，没事时线程去睡觉**。

```
传统阻塞模型（每客户端一线程）：
  线程 1: read(fd1) ← 阻塞，等客户端 1 发数据
  线程 2: read(fd2) ← 阻塞，等客户端 2 发数据
  线程 3: read(fd3) ← 阻塞
  → 1 万个客户端 = 1 万个线程 = 80GB 栈内存

I/O 复用模型（epoll）：
  一个线程: epoll_wait(所有 fd) ← 阻塞，但同时在监听 1000 个 fd
  谁有事就处理谁，没事就继续等
  → 1 个线程扛 1 万个客户端
```

## epoll 三件套

| API | 作用 |
|-----|------|
| `epoll_create1(0)` | 创建 epoll 实例，返回 epoll fd |
| `epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)` | 把 fd 注册进 epoll |
| `epoll_wait(epfd, events, max, -1)` | 阻塞等待事件，返回就绪事件数组 |

## 核心代码

```cpp
int epfd = epoll_create1(0);
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);  // 监听 fd 注册

while(true) {
    int nfds = epoll_wait(epfd, event, MAX_EVENTS, -1);  // 阻塞等待
    for(int i = 0; i < nfds; i++) {
        if(event[i].data.fd == sockfd) {
            // 监听 fd 可读 → 有新客户端
            int client_fd = accept(sockfd, ...);
            epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);  // 新 fd 也注册
        } else if(event[i].events & EPOLLIN) {
            // 客户端 fd 可读 → 读数据 + echo
            while(true) {
                read(client_fd, buffer, sizeof(buffer));
                write(client_fd, buffer, bytes_read);
                // 读到 EAGAIN 退出循环
            }
        }
    }
}
```

## 关键点

- `epoll_wait` 返回的是**快照数组**，for 循环依次处理
- 处理期间新就绪的 fd 不会插队，等**下一轮** epoll_wait 再处理
- 边沿触发（ET）配合 `while` 循环，必须读到 EAGAIN 才停
- 同一 fd 的事件**总在同一线程处理**（保证无竞态）

## 与上一版的差异

| | 2_EchoAndUtil | 3_Epoll |
|---|--------------|---------|
| 客户端数量 | 只能 1 个 | 可同时多个 |
| 模型 | 阻塞 read | I/O 复用 |
| 核心 API | read/write | epoll_create/ctl/wait |

## 这版的限制

- 所有逻辑堆在 main 函数里（if/else 判断 fd 类型）
- fd 管理用裸 `epoll_event` 数组，没有封装
- 代码不可维护，一长就乱

## 与下一版的衔接

第 4 版将把 epoll + socket 封装成类，代码从「裸 API 调用」变成「面向对象」。
