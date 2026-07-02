#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <type_traits>

namespace photon {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    void waitAll();
    size_t threadCount() const { return m_threads.size(); }

private:
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_completionCondition;
    std::atomic<bool> m_stop{false};
    std::atomic<size_t> m_activeTasks{0};
};

// Template implementation
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) {
            throw std::runtime_error("Cannot submit to stopped ThreadPool");
        }
        m_activeTasks++;
        m_tasks.emplace([task, this]() {
            (*task)();
            m_activeTasks--;
            m_completionCondition.notify_all();
        });
    }
    m_condition.notify_one();
    return result;
}

} // namespace photon
