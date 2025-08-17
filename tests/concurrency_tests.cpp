#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "wq/concurrency.hpp"

using namespace wq;

static void assert_true(bool cond, std::string_view msg)
{
    if (!cond)
    {
        std::cerr << "ASSERT FAILED: " << msg << std::endl;
        std::exit(1);
    }
}

static void test_batched_call_count_seq_par()
{ {
        std::atomic<int> c{0};
        for_each_index_batched(
            17,
            1,
            [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
        assert_true(c.load() == 17, "seq: call count == total");
    } {
        std::atomic<int> c{0};
        for_each_index_batched(
            101,
            4,
            [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
        assert_true(c.load() == 101, "par: call count == total");
    }
}

static void test_batched_cancelable_exception_propagation()
{
    std::atomic<int> c{0};
    bool caught = false;
    try
    {
        for_each_index_batched_cancelable(
            50,
            5,
            [&](int idx, const std::atomic<bool> &)
            {
                if (idx == 13) throw std::runtime_error("boom");
                c.fetch_add(1, std::memory_order_relaxed);
            });
    }
    catch (const std::exception &)
    {
        caught = true;
    }
    assert_true(caught, "exception propagated");
    assert_true(
        c.load() >= 0 && c.load() <= 50,
        "some work may have completed before cancel");
}

static void test_batched_cancelable_external_cancel()
{
    std::atomic<int> c{0};
    Cancellation cancel;
    cancel.cancel(); // pre-cancel
    for_each_index_batched_cancelable(
        100,
        8,
        [&](int, const std::atomic<bool> &flag)
        {
            if (flag.load(std::memory_order_relaxed)) return;
            c.fetch_add(1, std::memory_order_relaxed);
        },
        &cancel);
    assert_true(c.load() == 0, "pre-cancel -> no work");
}

static void test_batched_cancelable_seq_cancel_midway()
{
    std::atomic<int> c{0};
    Cancellation cancel;
    for_each_index_batched_cancelable(
        20,
        1,
        [&](int idx, const std::atomic<bool> &)
        {
            c.fetch_add(1, std::memory_order_relaxed);
            if (idx == 5) cancel.cancel();
        },
        &cancel);
    assert_true(c.load() == 5, "seq cancel at 5 -> 5 calls");
}

static void test_batched_small_load_1000()
{
    std::atomic<int> c{0};
    for_each_index_batched(
        1000,
        8,
        [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
    assert_true(c.load() == 1000, "1000 tasks finished");
}

static void test_pool_submit_and_wait()
{
    ThreadPool pool(4);
    std::atomic<int> c{0};
    for (int i = 0; i < 100; ++i)
    {
        pool.submit([&] { c.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.wait_idle();
    assert_true(c.load() == 100, "pool completes all tasks");
    assert_true(pool.first_exception() == nullptr, "no exception recorded");
}

static void test_pool_exception_and_cancel()
{
    ThreadPool pool(3);
    std::atomic<int> c{0};
    for (int i = 0; i < 10; ++i)
    {
        if (i == 5)
        {
            pool.submit([] { throw std::runtime_error("boom"); });
        }
        else
        {
            pool.submit([&] { c.fetch_add(1, std::memory_order_relaxed); });
        }
    }
    pool.wait_idle();
    assert_true(pool.first_exception() != nullptr, "first exception captured");
}

static void test_pool_cancel_cooperative()
{
    ThreadPool pool(4);
    std::atomic<int> c{0};
    for (int i = 0; i < 200; ++i)
    {
        pool.submit_cancelable(
            [&](const std::atomic<bool> &flag)
            {
                if (flag.load(std::memory_order_relaxed)) return;
                // cooperatively stop
                c.fetch_add(1, std::memory_order_relaxed);
            });
    }
    pool.cancel();
    pool.wait_idle();
    assert_true(c.load() < 200, "cancel reduces completed tasks");
}

static void test_batched_total_zero()
{
    std::atomic<int> c{0};
    for_each_index_batched(
        0,
        4,
        [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
    assert_true(c.load() == 0, "total=0 -> no calls");
}

static void test_pool_wait_idle_no_tasks()
{
    ThreadPool pool(2);
    pool.wait_idle();
    assert_true(true, "wait_idle with no tasks returns");
}

// ---- Boundary/robustness tests ----
static void test_batched_concurrency_le1_branches()
{
    // for_each_index_batched with concurrency 0 and -1 behaves as sequential
    {
        std::atomic<int> c{0};
        for_each_index_batched(
            10,
            0,
            [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
        assert_true(c.load() == 10, "concurrency=0 -> sequential");
    } {
        std::atomic<int> c{0};
        for_each_index_batched(
            10,
            -1,
            [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
        assert_true(c.load() == 10, "concurrency=-1 -> sequential");
    }
    // cancelable variant
    {
        std::atomic<int> c{0};
        for_each_index_batched_cancelable(
            10,
            0,
            [&](int, const std::atomic<bool> &)
            {
                c.fetch_add(1, std::memory_order_relaxed);
            });
        assert_true(c.load() == 10, "cancelable concurrency=0 -> sequential");
    } {
        std::atomic<int> c{0};
        for_each_index_batched_cancelable(
            10,
            -1,
            [&](int, const std::atomic<bool> &)
            {
                c.fetch_add(1, std::memory_order_relaxed);
            });
        assert_true(c.load() == 10, "cancelable concurrency=-1 -> sequential");
    }
}

static void test_batched_over_parallelism()
{
    // concurrency > total must still call exactly total times
    {
        std::atomic<int> c{0};
        for_each_index_batched(
            5,
            64,
            [&](int) { c.fetch_add(1, std::memory_order_relaxed); });
        assert_true(c.load() == 5, "over-parallelism batched");
    } {
        std::atomic<int> c{0};
        for_each_index_batched_cancelable(
            5,
            64,
            [&](int, const std::atomic<bool> &)
            {
                c.fetch_add(1, std::memory_order_relaxed);
            });
        assert_true(c.load() == 5, "over-parallelism batched cancelable");
    }
}

static void test_pool_multiple_exceptions_first_only()
{
    ThreadPool pool(8);
    for (int i = 0; i < 10; ++i)
    {
        if (i % 2 == 0) pool.submit([] { throw std::runtime_error("boom"); });
        else pool.submit([] {});
    }
    pool.wait_idle();
    auto ep = pool.first_exception();
    assert_true(ep != nullptr, "first exception captured when multiple thrown");
    // cancel flag should be set after exception
    assert_true(
        pool.cancel_flag().load(std::memory_order_relaxed),
        "cancel flag set after exception");
    // Subsequent wait_idle calls should be safe
    pool.wait_idle();
}

static void test_pool_cancel_idempotent_and_post_cancel_submit()
{
    ThreadPool pool(4);
    pool.cancel();
    pool.cancel(); // idempotent
    std::atomic<int> cancelable_count{0};
    for (int i = 0; i < 50; ++i)
    {
        pool.submit_cancelable(
            [&](const std::atomic<bool> &flag)
            {
                if (flag.load(std::memory_order_relaxed)) return;
                // should early-return after cancel
                cancelable_count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    std::atomic<int> non_cancelable_count{0};
    for (int i = 0; i < 10; ++i)
    {
        pool.submit(
            [&]
            {
                non_cancelable_count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    pool.wait_idle();
    assert_true(
        cancelable_count.load() == 0,
        "post-cancel submit_cancelable should do nothing");
    assert_true(
        non_cancelable_count.load() == 10,
        "post-cancel submit should still run");
}

static void test_pool_wait_idle_with_long_tasks()
{
    ThreadPool pool(4);
    using namespace std::chrono;
    std::atomic<int> c{0};
    // 2 long tasks (~50ms), 20 short tasks
    for (int i = 0; i < 2; ++i)
    {
        pool.submit(
            [&]
            {
                std::this_thread::sleep_for(milliseconds(50));
                c.fetch_add(1, std::memory_order_relaxed);
            });
    }
    for (int i = 0; i < 20; ++i)
    {
        pool.submit([&] { c.fetch_add(1, std::memory_order_relaxed); });
    }
    auto t0 = steady_clock::now();
    pool.wait_idle();
    auto dt = duration_cast<milliseconds>(steady_clock::now() - t0);
    assert_true(c.load() == 22, "all long+short tasks completed");
    assert_true(
        dt.count() >= 40,
        "wait_idle blocks until long tasks complete (>=40ms)");
}

int main()
{
    test_batched_call_count_seq_par();
    test_batched_cancelable_exception_propagation();
    test_batched_cancelable_external_cancel();
    test_batched_cancelable_seq_cancel_midway();
    test_batched_small_load_1000();
    test_pool_submit_and_wait();
    test_pool_exception_and_cancel();
    test_pool_cancel_cooperative();
    test_batched_total_zero();
    test_pool_wait_idle_no_tasks();
    test_batched_concurrency_le1_branches();
    test_batched_over_parallelism();
    test_pool_multiple_exceptions_first_only();
    test_pool_cancel_idempotent_and_post_cancel_submit();
    test_pool_wait_idle_with_long_tasks();

    std::cout << "concurrency tests: OK" << std::endl;
    return 0;
}
