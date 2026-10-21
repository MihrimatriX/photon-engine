// thread_pool.cpp — ThreadPool'un işçi döngüsü, kapatma ve waitAll uygulaması.
#include "thread_pool.h"

namespace photon {

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = 1;
    }

    m_threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_threads.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_condition.wait(lock, [this]() {
                        return m_stop || !m_tasks.empty();
                    });

                    // Kapanırken kuyrukta kalan işler yine de bitirilir.
                    if (m_stop && m_tasks.empty()) {
                        return;
                    }

                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_condition.notify_all();

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_completionCondition.wait(lock, [this]() {
        return m_activeTasks == 0 && m_tasks.empty();
    });
}

} // namespace photon
