#include "Engine/Core/TimeSlicedJobSystem.h"

#include <GLFW/glfw3.h>

namespace Engine {

    void TimeSlicedJobSystem::Enqueue(Job job) {
        if (!job) return;
        m_Jobs.push(std::move(job));
    }

    int TimeSlicedJobSystem::RunForBudgetMs(double budgetMs) {
        if (budgetMs <= 0.0) return 0;

        const double startMs = glfwGetTime() * 1000.0;
        int completed = 0;

        while (!m_Jobs.empty()) {
            const double nowMs = glfwGetTime() * 1000.0;
            const double elapsedMs = nowMs - startMs;
            const double remainingMs = budgetMs - elapsedMs;
            if (remainingMs <= 0.0) break;

            Job job = std::move(m_Jobs.front());
            m_Jobs.pop();

            const bool done = job(remainingMs);
            if (!done) {
                m_Jobs.push(std::move(job));
            }
            completed++;
        }

        return completed;
    }

    int TimeSlicedJobSystem::PendingCount() const {
        return (int)m_Jobs.size();
    }

}
