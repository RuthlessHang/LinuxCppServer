#pragma once
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class ThreadPool
{
public:
    ThreadPool(size_t threadNum = 10);
    ~ThreadPool();

    template<class F, class... Args>
    auto add(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(const ThreadPool&) = delete;

private:
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
    std::atomic<bool> m_stop;
    void workThread();
};

// 模板不能放在 cpp 文件，C++ 编译器不支持模板的分离编译
template<class F, class... Args>
auto ThreadPool::add(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_stop)
            throw std::runtime_error("ThreadPool already stop, can't add task any more");
        m_tasks.emplace([task](){ (*task)(); });
    }
    m_cv.notify_one();
    return res;
}
