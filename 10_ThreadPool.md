# 10_ThreadPool — 线程池框架（过渡版本）

## 这版解决什么问题

搭建线程池框架 `ThreadPool`，为下一版主从 Reactor 多线程模式做准备。**实际上没有启用多线程**——是过渡版本。

## 新增 ThreadPool 类

### 接口

```cpp
class ThreadPool {
public:
    ThreadPool(size_t threadNum = 10);
    ~ThreadPool();
    template<class F, class... Args>
    auto add(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
    std::atomic<bool> m_stop;
    void workThread();
};
```

### 构造函数：开 N 个线程

```cpp
ThreadPool::ThreadPool(size_t threadNum):m_stop(false) {
    for(size_t i = 0; i < threadNum; ++i) {
        m_threads.emplace_back([this]() { workThread(); });
    }
}
```

N 个线程启动，全部进入 `workThread()` 死循环，在 `m_cv.wait` 上睡觉。

### workThread：每个线程的死循环

```cpp
void ThreadPool::workThread() {
    while (true) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this](){
            return m_stop || !m_tasks.empty();  // 没停且有任务才醒
        });
        if(m_stop && m_tasks.empty()) return;  // 停了且没任务 → 退出
        std::function<void()> task(std::move(m_tasks.front()));
        m_tasks.pop();
        lock.unlock();   // 先解锁再执行，让别的线程能取任务
        task();
    }
}
```

### add：通用任务接口

```cpp
template<class F, class... Args>
auto ThreadPool::add(F&& f, Args&&... args) -> std::future<...> {
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_stop) throw std::runtime_error("...");
        m_tasks.emplace([task](){ (*task)(); });
    }
    m_cv.notify_one();
    return res;
}
```

**打包链**：`bind(函数, 参数)` → 无参 → `packaged_task` → 承诺返回值 → `shared_ptr` → 绕过不可拷贝 → lambda → 匹配队列类型

### 析构函数：优雅停止

```cpp
ThreadPool::~ThreadPool() {
    m_stop = true;        // ① 设停止标志
    m_cv.notify_all();    // ② 唤醒所有睡觉的线程
    for(auto& thread : m_threads) {
        if(thread.joinable()) thread.join();  // ③ 等每个线程退出
    }
}
```

## EventLoop 的改动（但没启用）

```cpp
class EventLoop {
public:
    void addThreadPool(std::function<void()> task);  // 新增接口
private:
    ThreadPool* m_threadPool;  // 新增成员
};

// Channel::handleEvent 里：
//m_loop->addThreadPool(m_Callback);  ← 注释掉了，没启用
m_Callback();                          ← 还是直接调用
```

## 与上一版的差异

| | 9_Buffer | 10_ThreadPool |
|---|---------|---------------|
| ThreadPool 类 | ❌ 没有 | ✅ 完整实现 |
| EventLoop 持有 ThreadPool | ❌ | ✅ 有（但没用） |
| Channel 回调执行方式 | 主线程直接调 | **还是主线程直接调**（没走线程池） |
| 业务流程 | 单线程 | **还是单线程** |

## 为什么是过渡版本

作者本来设想「Channel 回调丢进线程池执行」，但后来发现架构不合理（线程池不该归 EventLoop 管）。所以 day10 把 ThreadPool 工具类写好，留给 day12 用。Channel.cpp 里的注释 `//m_loop->addThreadPool(m_Callback);` 就是作者留下的「以后再改」标记。

## 这版的限制

- 线程池搭了但没用
- 业务仍是单线程串行处理
- 等待 day12 真正启用主从 Reactor 架构

## 与下一版的衔接

第 11 版（day12）将**真正使用** ThreadPool：Server 持有线程池，每个 subReactor 一个线程，fd%N 派发，实现多线程并发。
