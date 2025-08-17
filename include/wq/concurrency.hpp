#pragma once

#include <functional>
#include <atomic>
#include <exception>

namespace wq {

// Execute fn(index) for index = 1..total with at most `concurrency` threads at a time.
// If concurrency <= 1, runs sequentially.
// The function guarantees that all tasks complete before returning.
void for_each_index_batched(int total, int concurrency, const std::function<void(int)>& fn);

// Simple cancellation handle
class Cancellation {
public:
  void cancel() { flag_.store(true, std::memory_order_relaxed); }
  bool is_cancelled() const { return flag_.load(std::memory_order_relaxed); }
  const std::atomic<bool>& flag() const { return flag_; }
private:
  std::atomic<bool> flag_{false};
};

// Cancellation-aware variant.
// - If `cancel` is provided and set during execution, tasks will observe cancellation via the token
//   and new tasks will stop being scheduled.
// - Exceptions thrown from fn will be propagated (first exception wins) after all running tasks join.
void for_each_index_batched_cancelable(
    int total,
    int concurrency,
    const std::function<void(int, const std::atomic<bool>&)>& fn,
    Cancellation* cancel = nullptr);

// Simple fixed-size thread pool
class ThreadPool {
public:
  explicit ThreadPool(int threads);
  ~ThreadPool();

  // Enqueue a non-cancelable task
  void submit(std::function<void()> task);

  // Enqueue a task that can observe pool's cancellation flag
  void submit_cancelable(std::function<void(const std::atomic<bool>&)> task);

  // Wait until the queue is empty and all tasks complete
  void wait_idle();

  // Request cooperative cancellation (tasks should check cancel_flag())
  void cancel();
  const std::atomic<bool>& cancel_flag() const;

  // Returns first captured exception (if any); nullptr if none
  std::exception_ptr first_exception() const;

private:
  struct Impl;
  Impl* impl_;
};

} // namespace wq
