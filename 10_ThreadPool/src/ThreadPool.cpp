#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t threadNum):m_stop(false)
{
    for(size_t i = 0; i < threadNum; ++i)
    {
        m_threads.emplace_back([this]() {
           workThread();
        });
    }
}

ThreadPool::~ThreadPool()
{
    m_stop = true;
    m_cv.notify_all();
    for(auto& thread : m_threads)
    {
        if(thread.joinable())
        {
            thread.join();
        }
    }
}
    


void ThreadPool::workThread()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock , [this](){
            return m_stop || !m_tasks.empty();
        });

        if(m_stop && m_tasks.empty())
        {
            return;
        }

        std::function<void()> task(std::move(m_tasks.front()));
        m_tasks.pop();
        lock.unlock();
        task();
    }
    
}
