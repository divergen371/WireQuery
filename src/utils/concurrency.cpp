#include "wq/concurrency.hpp"

#include <algorithm>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <memory>

namespace wq {

void for_each_index_batched(int total, int concurrency, const std::function<void(int)>& fn)
{
    if (total <= 0) return;

    if (concurrency <= 1)
    {
        for (int i = 1; i <= total; ++i) fn(i);
        return;
    }

    int next = 1;
    while (next <= total)
    {
        const int batch = std::min(concurrency, total - (next - 1));
        std::vector<std::thread> threads;
        threads.reserve(batch);
        for (int i = 0; i < batch; ++i)
        {
            const int index = next + i;
            threads.emplace_back([&, index] { fn(index); });
        }
        for (auto& th : threads)
        {
            if (th.joinable()) th.join();
        }
        next += batch;
    }
}

void for_each_index_batched_cancelable(
    int total,
    int concurrency,
    const std::function<void(int, const std::atomic<bool>&)>& fn,
    Cancellation* cancel)
{
    if (total <= 0) return;

    std::atomic<bool> local_cancel{false};
    const std::atomic<bool>& flag = cancel ? cancel->flag() : local_cancel;

    std::mutex ex_mtx;
    std::exception_ptr first_ex = nullptr;
    auto set_first_ex = [&](std::exception_ptr ep) {
        if (!ep) return;
        std::scoped_lock lk(ex_mtx);
        if (!first_ex) first_ex = std::move(ep);
    };

    auto safe_call = [&](int idx) {
        if (flag.load(std::memory_order_relaxed)) return;
        try {
            fn(idx, flag);
        } catch (...) {
            set_first_ex(std::current_exception());
            if (cancel) cancel->cancel(); else local_cancel.store(true, std::memory_order_relaxed);
        }
    };

    if (concurrency <= 1)
    {
        for (int i = 1; i <= total; ++i)
        {
            if (flag.load(std::memory_order_relaxed)) break;
            safe_call(i);
        }
    }
    else
    {
        int next = 1;
        while (next <= total && !flag.load(std::memory_order_relaxed))
        {
            const int remain = total - (next - 1);
            const int batch = std::min(concurrency, remain);
            std::vector<std::thread> threads;
            threads.reserve(batch);
            for (int i = 0; i < batch; ++i)
            {
                const int idx = next + i;
                threads.emplace_back([&, idx] { safe_call(idx); });
            }
            for (auto& th : threads) if (th.joinable()) th.join();
            next += batch;
        }
    }

    if (first_ex) std::rethrow_exception(first_ex);
}

// ---------------------- ThreadPool implementation ----------------------

struct ThreadPool::Impl {
    explicit Impl(int n)
        : stop(false), active(0), cancel(false)
    {
        if (n <= 0) n = 1;
        workers.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            workers.emplace_back([this]{ this->worker_loop(); });
        }
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            stop = true;
        }
        cv_task.notify_all();
        for (auto& th : workers) if (th.joinable()) th.join();
    }

    void submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (stop) return; // ignore submissions after stop
            q.push(std::move(task));
        }
        cv_task.notify_one();
    }

    void submit_cancelable(std::function<void(const std::atomic<bool>&)> task)
    {
        submit([this, t = std::move(task)](){ t(cancel); });
    }

    void wait_idle()
    {
        std::unique_lock<std::mutex> lk(mtx);
        cv_idle.wait(lk, [&]{ return q.empty() && active == 0; });
    }

    void cancel_req()
    {
        cancel.store(true, std::memory_order_relaxed);
    }

    const std::atomic<bool>& cancel_flag() const { return cancel; }

    std::exception_ptr first_exception() const
    {
        std::lock_guard<std::mutex> lk(mtx);
        return first_ex;
    }

    void set_first_exception(std::exception_ptr ep)
    {
        if (!ep) return;
        std::lock_guard<std::mutex> lk(mtx);
        if (!first_ex) first_ex = std::move(ep);
    }

private:
    void worker_loop()
    {
        for(;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv_task.wait(lk, [&]{ return stop || !q.empty(); });
                if (stop && q.empty()) return;
                task = std::move(q.front());
                q.pop();
                ++active;
            }
            try {
                task();
            } catch (...) {
                set_first_exception(std::current_exception());
                cancel_req();
            }
            {
                std::lock_guard<std::mutex> lk(mtx);
                --active;
                if (q.empty() && active == 0) cv_idle.notify_all();
            }
        }
    }

private:
    mutable std::mutex mtx;
    std::condition_variable cv_task;
    std::condition_variable cv_idle;
    std::queue<std::function<void()>> q;
    std::vector<std::thread> workers;
    bool stop;
    size_t active;
    std::atomic<bool> cancel;
    std::exception_ptr first_ex;
};

ThreadPool::ThreadPool(int threads)
  : impl_(new Impl(threads))
{}

ThreadPool::~ThreadPool()
{
    delete impl_;
}

void ThreadPool::submit(std::function<void()> task)
{
    impl_->submit(std::move(task));
}

void ThreadPool::submit_cancelable(std::function<void(const std::atomic<bool>&)> task)
{
    impl_->submit_cancelable(std::move(task));
}

void ThreadPool::wait_idle()
{
    impl_->wait_idle();
}

void ThreadPool::cancel()
{
    impl_->cancel_req();
}

const std::atomic<bool>& ThreadPool::cancel_flag() const
{
    return impl_->cancel_flag();
}

std::exception_ptr ThreadPool::first_exception() const
{
    return impl_->first_exception();
}

} // namespace wq
