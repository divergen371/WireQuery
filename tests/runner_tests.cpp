#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>

#include "wq/runner.hpp"
#include "wq/options.hpp"

using namespace wq;

static void assert_true(bool cond, const char *msg)
{
    if (!cond)
    {
        std::cerr << "ASSERT FAILED: " << msg << std::endl;
        std::exit(1);
    }
}

static void assert_eq_int(int a, int b, const char *msg)
{
    if (a != b)
    {
        std::cerr << "ASSERT FAILED: " << msg << " | expected=" << b <<
                " actual=" << a << std::endl;
        std::exit(1);
    }
}

static Options base_posix_opt()
{
    Options opt{};
    opt.host = "127.0.0.1"; // numeric host for fast path
    opt.numeric_host = true;
    opt.tries = 1;
    opt.concurrency = 1;
    return opt;
}

static Options base_rawdns_opt()
{
    Options opt{};
    opt.host = "example.com";
    opt.qtype = "A"; // use rawdns path (will be NotAvailable if ldns missing)
    opt.ns = "1.1.1.1"; // arbitrary
    opt.tries = 1;
    opt.concurrency = 1;
    return opt;
}

static void test_posix_tries1()
{
    Options opt = base_posix_opt();
    opt.tries = 1;
    std::vector<int> seq;
    auto on_try = [&](int t,
                      double /*ms*/,
                      const AttemptResult * /*posix*/,
                      const RawDnsResult * /*raw*/)
    {
        seq.push_back(t);
    };
    auto times = run_posix_queries(opt, on_try);
    assert_eq_int(
        static_cast<int>(times.size()),
        1,
        "posix tries1: times size");
    assert_eq_int(
        static_cast<int>(seq.size()),
        1,
        "posix tries1: on_try calls");
    assert_eq_int(seq[0], 1, "posix tries1: order");
}

static void test_posix_tries2_seq_order()
{
    Options opt = base_posix_opt();
    opt.tries = 2;
    opt.concurrency = 1; // deterministic order 1->2
    std::vector<int> seq;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
    };
    auto times = run_posix_queries(opt, on_try);
    assert_eq_int(
        static_cast<int>(times.size()),
        2,
        "posix tries2: times size");
    assert_eq_int(
        static_cast<int>(seq.size()),
        2,
        "posix tries2: on_try calls");
    assert_true(seq[0] == 1 && seq[1] == 2, "posix tries2: order 1,2");
}

static void test_posix_on_try_exception_stops()
{
    Options opt = base_posix_opt();
    opt.tries = 3;
    opt.concurrency = 1; // sequential, deterministic
    opt.stop_on_error = true; // 例外で短絡
    std::vector<int> seq;
    bool thrown = false;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
        if (t == 2) throw std::runtime_error("boom");
    };
    try
    {
        (void) run_posix_queries(opt, on_try);
    }
    catch (const std::exception &)
    {
        thrown = true;
    }
    assert_true(thrown, "posix on_try exception should propagate");
    assert_eq_int(
        static_cast<int>(seq.size()),
        2,
        "posix on_try exception: only 1 and 2 called");
    assert_true(
        seq[0] == 1 && seq[1] == 2,
        "posix on_try exception: order up to throw");
}

static void test_posix_on_try_exception_continues()
{
    Options opt = base_posix_opt();
    opt.tries = 3;
    opt.concurrency = 1;
    opt.stop_on_error = false; // 継続モード
    std::vector<int> seq;
    bool thrown = false;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
        if (t == 2) throw std::runtime_error("boom");
    };
    try
    {
        (void) run_posix_queries(opt, on_try);
    }
    catch (...)
    {
        thrown = true;
    }
    assert_true(!thrown, "posix continue mode should not throw");
    assert_eq_int(static_cast<int>(seq.size()), 3, "posix continue: all tries called");
    assert_true(seq[0] == 1 && seq[1] == 2 && seq[2] == 3, "posix continue: order 1,2,3");
}

static void test_rawdns_tries1()
{
    Options opt = base_rawdns_opt();
    opt.tries = 1;
    std::vector<int> seq;
    auto on_try = [&](int t,
                      double /*ms*/,
                      const AttemptResult * /*posix*/,
                      const RawDnsResult * /*raw*/)
    {
        seq.push_back(t);
    };
    auto times = run_rawdns_queries(opt, on_try);
    assert_eq_int(
        static_cast<int>(times.size()),
        1,
        "rawdns tries1: times size");
    assert_eq_int(
        static_cast<int>(seq.size()),
        1,
        "rawdns tries1: on_try calls");
    assert_eq_int(seq[0], 1, "rawdns tries1: order");
}

static void test_rawdns_tries2_seq_order()
{
    Options opt = base_rawdns_opt();
    opt.tries = 2;
    opt.concurrency = 1; // deterministic 1->2
    std::vector<int> seq;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
    };
    auto times = run_rawdns_queries(opt, on_try);
    assert_eq_int(
        static_cast<int>(times.size()),
        2,
        "rawdns tries2: times size");
    assert_eq_int(
        static_cast<int>(seq.size()),
        2,
        "rawdns tries2: on_try calls");
    assert_true(seq[0] == 1 && seq[1] == 2, "rawdns tries2: order 1,2");
}

static void test_rawdns_on_try_exception_stops()
{
    Options opt = base_rawdns_opt();
    opt.tries = 3;
    opt.concurrency = 1;
    opt.stop_on_error = true; // 例外で短絡
    std::vector<int> seq;
    bool thrown = false;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
        if (t == 2) throw std::runtime_error("boom");
    };
    try
    {
        (void) run_rawdns_queries(opt, on_try);
    }
    catch (const std::exception &)
    {
        thrown = true;
    }
    assert_true(thrown, "rawdns on_try exception should propagate");
    assert_eq_int(
        (int) seq.size(),
        2,
        "rawdns on_try exception: only 1 and 2 called");
    assert_true(
        seq[0] == 1 && seq[1] == 2,
        "rawdns on_try exception: order up to throw");
}

static void test_rawdns_on_try_exception_continues()
{
    Options opt = base_rawdns_opt();
    opt.tries = 3;
    opt.concurrency = 1;
    opt.stop_on_error = false; // 継続モード
    std::vector<int> seq;
    bool thrown = false;
    auto on_try = [&](int t,
                      double,
                      const AttemptResult *,
                      const RawDnsResult *)
    {
        seq.push_back(t);
        if (t == 2) throw std::runtime_error("boom");
    };
    try
    {
        (void) run_rawdns_queries(opt, on_try);
    }
    catch (...)
    {
        thrown = true;
    }
    assert_true(!thrown, "rawdns continue mode should not throw");
    assert_eq_int((int) seq.size(), 3, "rawdns continue: all tries called");
    assert_true(seq[0] == 1 && seq[1] == 2 && seq[2] == 3, "rawdns continue: order 1,2,3");
}

int main()
{
    test_posix_tries1();
    test_posix_tries2_seq_order();
    test_posix_on_try_exception_stops();
    test_posix_on_try_exception_continues();

    test_rawdns_tries1();
    test_rawdns_tries2_seq_order();
    test_rawdns_on_try_exception_stops();
    test_rawdns_on_try_exception_continues();

    std::cout << "runner tests: OK" << std::endl;
    return 0;
}
