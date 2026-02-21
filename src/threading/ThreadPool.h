#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Threading {

    class ThreadPool {
    public:
        explicit ThreadPool(std::size_t threadCount = 0);
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        void Enqueue(std::function<void()> job);
        void EnqueueUrgent(std::function<void()> job);
        void Stop();

        std::size_t GetThreadCount() const { return m_Threads.size(); }

    private:
        void WorkerLoop();

        std::vector<std::thread> m_Threads;
        std::mutex m_Mutex;
        std::condition_variable m_CV;
        std::queue<std::function<void()>> m_UrgentJobs;
        std::queue<std::function<void()>> m_LazyJobs;
        bool m_Stopping = false;
    };

}
