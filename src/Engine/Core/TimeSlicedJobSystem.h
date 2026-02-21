#pragma once

#include <functional>
#include <queue>

namespace Engine {

    class TimeSlicedJobSystem {
    public:
        using Job = std::function<bool(double budgetMs)>;

        void Enqueue(Job job);
        int RunForBudgetMs(double budgetMs);

        int PendingCount() const;

    private:
        std::queue<Job> m_Jobs;
    };

}
