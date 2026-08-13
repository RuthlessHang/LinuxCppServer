# 10_ThreadPool 逻辑总结

## 一、本节核心：给服务器加上线程池

这节在 9_Buffer 的基础上，**只新增了线程池**，让 EventLoop 持有一个 ThreadPool，为后续 day12 主从 Reactor 多线程模式做准备。

### 一句话总结

> 把 ThreadPool 类写好，挂到 EventLoop 上，但 loop() 还是直接处理事件，线程池只是"装上去了"，真正用起来要等 day12。

---

## 二、与 9_Buffer 的对比

### 改了什么

| 文件 | 9_Buffer | 10_ThreadPool | 改动说明 |
|------|----------|---------------|---------|
| **ThreadPool.hpp** | 不存在 | **新增** | 线程池类，`add` 模板方法 |
| **ThreadPool.cpp** | 不存在 | **新增** | 线程池实现 |
| **EventLoop.hpp** | 只有 `Epoll*` | 新增 `ThreadPool* m_threadPool` | 持有线程池 |
| **EventLoop.cpp** | `new Epoll()` | 加了 `new ThreadPool()` + `addThreadPool()` | 创建线程池 + 提供提交任务接口 |
| **Channel.cpp** | `m_Callback()` | `m_Callback()`（注释了 `addThreadPool`） | 暂时还是直接执行 |
| **Server.cpp** | 无 `delete clientAddr` | 加了 `delete clientAddr` | 修复内存泄漏 |
| **Connection.cpp** | write 失败只打印 | write 失败触发断开回调 | 修复僵尸连接 |
| **Buffer.hpp/cpp** | 无 | 无 | 和 9_Buffer 完全一致 |
| **Makefile** | 无 `-pthread` | 加了 `-std=c++17 -pthread -g` | 支持多线程和 C++17 |

### 没改什么

- Epoll、Socket、InetAddress、Acceptor、Server（除内存泄漏修复外）、Connection（除 write 失败处理外）的逻辑都没变
- 事件循环 `loop()` 还是直接处理事件
- Buffer 的核心功能（append、c_str、clear）没变

---

## 三、Buffer 的说明

Buffer 类和 9_Buffer **完全一致**，没有任何改动。之前为了 test.cpp 加过一个 `setBuf` 方法，但 test.cpp 已经删除，`setBuf` 也一并删除了，现在 Buffer 就是原来的样子。

---

## 四、ThreadPool 类设计

### 核心成员

```cpp
class ThreadPool {
private:
    std::vector<std::thread> m_threads;          // 工作线程数组
    std::queue<std::function<void()>> m_tasks;   // 任务队列
    std::mutex m_mutex;                          // 保护任务队列的锁
    std::condition_variable m_cv;                // 唤醒等待的线程
    std::atomic<bool> m_stop;                    // 停止标志（原子操作）
    void workThread();                           // 工作线程函数
};
```

### 工作原理

```
                    任务队列 m_tasks
                   ┌──────────────────┐
add(task) ────────→│ task1 task2 task3│
                   └──────────────────┘
                          ↑    ↓
                     notify_one  取出
                          │    │
        ┌─────────────────┼────┼─────────────────┐
        ↓                 ↓    ↓                 ↓
   ┌─────────┐      ┌─────────┐           ┌─────────┐
   │thread 1 │      │thread 2 │    ...    │thread N │
   │wait()   │      │task()   │           │wait()   │
   └─────────┘      └─────────┘           └─────────┘
```

### 工作线程的工作循环

```cpp
void ThreadPool::workThread()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // 等待：直到有任务 或 线程池停止
        m_cv.wait(lock, [this](){
            return m_stop || !m_tasks.empty();
        });

        // 线程池停止且任务空了 → 退出
        if(m_stop && m_tasks.empty())
            return;

        // 取出任务，解锁，执行
        std::function<void()> task(std::move(m_tasks.front()));
        m_tasks.pop();
        lock.unlock();      // ← 执行任务时不持锁，让其他线程能取任务
        task();             // ← 执行任务
    }
}
```

### 关键设计点

| 设计 | 为什么 |
|------|--------|
| `std::atomic<bool> m_stop` | 多线程读写，保证原子性 |
| `std::condition_variable` | 没任务时线程休眠，不占CPU |
| `lock.unlock()` 在 `task()` 之前 | 执行任务时不持锁，否则变成串行 |
| `if(t.joinable()) t.join()` | 防止重复 join 导致崩溃 |
| `delete` 拷贝构造和赋值 | 线程池不能被复制 |

---

## 五、add 模板方法

### add 逐行解析

```cpp
template<class F, class... Args>
auto ThreadPool::add(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    // 1. 推导返回类型
    using return_type = typename std::result_of<F(Args...)>::type;

    // 2. 把函数和参数绑定成 packaged_task，用 shared_ptr 管理
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    // 3. 获取 future（取货单）
    std::future<return_type> res = task->get_future();

    // 4. 加锁，把任务包装成 lambda 放入队列
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_stop)
            throw std::runtime_error("ThreadPool already stop");
        m_tasks.emplace([task](){ (*task)(); });
    }

    // 5. 唤醒一个等待的线程
    m_cv.notify_one();

    // 6. 返回 future
    return res;
}
```

### 为什么模板要放在 hpp 文件

```
模板不是真正的代码，而是"模具"
编译器需要看到调用时的具体类型才能生成代码

如果放在 .cpp：
  编译 ThreadPool.cpp 时，不知道 F 和 Args 是什么
  → 无法生成代码 → 链接报错

放在 .hpp：
  调用 pool->add(func) 时，编译器看到调用处
  → 知道 F 和 Args 的类型 → 现场生成代码
```

### add 的四种用法

```cpp
ThreadPool* pool = new ThreadPool();

// 用法1：无参无返回值
pool->add([](){
    std::cout << "hello" << std::endl;
});

// 用法2：有参数无返回值
void print(int a, double b);
pool->add(print, 1, 3.14);

// 用法3：有参数有返回值，获取结果
int add(int a, int b) { return a + b; }
std::future<int> result = pool->add(add, 1, 2);
int value = result.get();  // 阻塞等待，拿到 3

// 用法4：lambda 带返回值
std::future<int> result = pool->add([]() { return 42; });
std::cout << result.get() << std::endl;  // 输出 42

delete pool;
```

---

## 六、EventLoop 如何使用线程池

### EventLoop 持有线程池

```cpp
class EventLoop {
private:
    Epoll* m_epoll;
    ThreadPool* m_threadPool;  // 【新增】持有线程池
public:
    void addThreadPool(std::function<void()> task);  // 提交任务接口
};
```

### 事件循环（注意：还是直接处理事件）

```cpp
void EventLoop::loop()
{
    while (!quit)
    {
        std::vector<Channel*> activeChannel = m_epoll->poll();
        for (Channel* ch : activeChannel)
        {
            ch->handleEvent();  // ← 直接调用，不是丢给线程池
        }
    }
}
```

### Channel::handleEvent 当前状态

```cpp
void Channel::handleEvent()
{
    //m_loop->addThreadPool(m_Callback);  // ← 注释掉了，day12 再启用
    m_Callback();  // ← 目前还是直接执行
}
```

### 设计意图与隐患

```
当前的设计（线程池装好了但没用）：
- EventLoop::loop() 直接处理事件（同步）
- Channel::handleEvent() 直接执行回调（同步）
- ThreadPool 挂在 EventLoop 上，但 loop 没用它

如果启用 addThreadPool(callback) 会有竞态隐患：
- 主循环可能同时遍历 m_connections
- 线程池的工作线程可能同时调用 deleteConnection
- 两个线程同时操作 map 会崩溃

day12 才会真正解决：主从 Reactor 多线程模式
```

---

## 七、其他修复的问题

### 1. Server.cpp 内存泄漏

```cpp
// 9_Buffer 的问题：成功路径没释放 clientAddr
InetAddress* clientAddr = new InetAddress();
// ... accept 成功后 ...
m_connections[clientSocket->getSockfd()] = connection;
// ← 这里漏了 delete clientAddr

// 10_ThreadPool 修复：
m_connections[clientSocket->getSockfd()] = connection;
delete clientAddr;  // ← 加上这行
```

### 2. Connection.cpp write 失败处理

```cpp
// 9_Buffer 的问题：write 失败只打印，不断开
if(bytes_written == -1)
{
    std::cerr << "socket write failed: " << strerror(errno) << std::endl;
    // ← 没断开，僵尸连接留在 map 里
}

// 10_ThreadPool 修复：
if(bytes_written == -1)
{
    std::cerr << "socket write failed: " << strerror(errno) << std::endl;
    if(m_deleteConnectionCallback)
        m_deleteConnectionCallback(m_sock);  // ← 触发断开
    break;                                    // ← 退出循环
}
```

---

## 八、使用方法

### 编译

```bash
make server    # 编译服务器 + 客户端
make clean     # 清理编译产物
```

### 运行

```bash
# 终端1：启动服务器
./server

# 终端2：启动客户端
./client
```

### 使用体验

和 9_Buffer **完全一样**，因为线程池虽然装上了，但 `handleEvent` 还是直接执行回调，没有真正用线程池。线程池只是"待命"状态，等 day12 启用。
