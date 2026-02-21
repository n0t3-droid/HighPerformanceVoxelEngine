#include "threading/ThreadPool.h"

#include <algorithm>

namespace Threading {

    ThreadPool::ThreadPool(std::size_t threadCount) {
        if (threadCount == 0) {
            const unsigned int hw = std::max(1u, std::thread::hardware_concurrency());
            // Keep at least 1 worker, but don't steal the whole machine.
            threadCount = (hw > 2u) ? (std::size_t)(hw - 1u) : 1u;
        }

        m_Threads.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            m_Threads.emplace_back([this]() { WorkerLoop(); });
        }
    }

    ThreadPool::~ThreadPool() {
        Stop();
    }

    void ThreadPool::Enqueue(std::function<void()> job) {
        if (!job) return;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Stopping) return;
            m_LazyJobs.push(std::move(job));
        }
        m_CV.notify_one();
    }

    void ThreadPool::EnqueueUrgent(std::function<void()> job) {
        if (!job) return;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Stopping) return;
            m_UrgentJobs.push(std::move(job));
        }
        m_CV.notify_one();
    }

    void ThreadPool::Stop() {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Stopping) return;
            m_Stopping = true;
        }

        m_CV.notify_all();

        for (auto& t : m_Threads) {
            if (t.joinable()) t.join();
        }
        m_Threads.clear();

        // Drain any leftover jobs
        std::queue<std::function<void()>> empty;
        std::queue<std::function<void()>> emptyUrgent;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::swap(m_LazyJobs, empty);
            std::swap(m_UrgentJobs, emptyUrgent);
        }
    }

    void ThreadPool::WorkerLoop() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CV.wait(lock, [this]() {
                    return m_Stopping || !m_UrgentJobs.empty() || !m_LazyJobs.empty();
                });
                if (m_Stopping && m_UrgentJobs.empty() && m_LazyJobs.empty()) return;
                if (!m_UrgentJobs.empty()) {
                    job = std::move(m_UrgentJobs.front());
                    m_UrgentJobs.pop();
                } else if (!m_LazyJobs.empty()) {
                    job = std::move(m_LazyJobs.front());
                    m_LazyJobs.pop();
                }
            }
            if (job) job();
        }
    }

}
