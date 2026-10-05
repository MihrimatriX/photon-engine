// thread_pool.h — Sabit sayıda işçi thread'i olan basit görev havuzu.
//
// Render karoları (tile) bu havuza iş olarak atılır. Her işçi kuyruktan bir
// görev alır ve çalıştırır. submit() bir std::future döndürür; çağıran taraf
// future.get() ile işin bitmesini bekler ve işteki istisnayı (exception) alır.
#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
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
    // İkisi de yalnız m_mutex altında okunur/yazılır. Kilit dışında yazmak
    // "kayıp uyanma" yarışına yol açar: işçi koşulu kontrol edip uyumadan hemen
    // önce bayrak değişir ve notify kaçırılır; join() sonsuza kadar bekler.
    bool m_stop = false;
    size_t m_activeTasks = 0;
};

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [fn = std::forward<F>(f), ... bound = std::forward<Args>(args)]() mutable {
            return std::invoke(fn, bound...);
        });

    std::future<ReturnType> result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) {
            throw std::runtime_error("Cannot submit to stopped ThreadPool");
        }
        ++m_activeTasks;
        m_tasks.emplace([task, this]() {
            (*task)();
            {
                std::lock_guard<std::mutex> done(m_mutex);
                --m_activeTasks;
            }
            m_completionCondition.notify_all();
        });
    }
    m_condition.notify_one();
    return result;
}

} // namespace photon
