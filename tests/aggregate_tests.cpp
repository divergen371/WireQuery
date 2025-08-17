#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <cmath>
#include <limits>

#include "wq/aggregate.hpp"

using namespace wq;

static void assert_true(bool cond, std::string_view msg)
{
    if (!cond)
    {
        std::cerr << "ASSERT FAILED: " << msg << std::endl;
        std::exit(1);
    }
}

static bool approx(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

static double get_pct_value(const std::vector<std::pair<int,double>>& v, int p)
{
    for (const auto& kv : v)
    {
        if (kv.first == p) return kv.second;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

static int count_pct_key(const std::vector<std::pair<int,double>>& v, int p)
{
    int c = 0;
    for (const auto& kv : v) if (kv.first == p) ++c;
    return c;
}

static void test_empty()
{
    std::vector<double> t{};
    std::vector<int> pct{50};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(ag.min, 0.0) && approx(ag.avg, 0.0) && approx(ag.max, 0.0), "empty min/avg/max");
    assert_true(ag.percentiles.empty(), "empty percentiles empty");
}

static void test_singleton()
{
    std::vector<double> t{10.0};
    std::vector<int> pct{0,50,100};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(ag.min, 10.0) && approx(ag.avg, 10.0) && approx(ag.max, 10.0), "singleton min/avg/max");
    assert_true(approx(get_pct_value(ag.percentiles, 0), 10.0), "p0=10");
    assert_true(approx(get_pct_value(ag.percentiles, 50), 10.0), "p50=10");
    assert_true(approx(get_pct_value(ag.percentiles, 100), 10.0), "p100=10");
}

static void test_multiple_even()
{
    std::vector<double> t{4.0, 1.0, 3.0, 2.0}; // unsorted on purpose
    std::vector<int> pct{25,50,75,100};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(ag.min, 1.0) && approx(ag.avg, (4.0+1.0+3.0+2.0)/4.0) && approx(ag.max, 4.0), "even set min/avg/max");
    assert_true(approx(get_pct_value(ag.percentiles, 25), 1.0), "p25=1");
    assert_true(approx(get_pct_value(ag.percentiles, 50), 2.0), "p50=2");
    assert_true(approx(get_pct_value(ag.percentiles, 75), 3.0), "p75=3");
    assert_true(approx(get_pct_value(ag.percentiles, 100), 4.0), "p100=4");
}

static void test_clamp()
{
    std::vector<double> t{5.0, 7.0};
    std::vector<int> pct{-10, 150};
    Aggregation ag = aggregate_times(t, pct);
    // recorded keys remain as provided (-10,150), but values clamp to p0 and p100 respectively
    assert_true(approx(get_pct_value(ag.percentiles, -10), 5.0), "p-10 -> p0 value=5");
    assert_true(approx(get_pct_value(ag.percentiles, 150), 7.0), "p150 -> p100 value=7");
}

static void test_all_equal_values()
{
    std::vector<double> t{3.0, 3.0, 3.0, 3.0};
    std::vector<int> pct{0,25,50,75,100};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(ag.min, 3.0) && approx(ag.avg, 3.0) && approx(ag.max, 3.0), "all equal min/avg/max");
    for (int p : pct)
    {
        assert_true(approx(get_pct_value(ag.percentiles, p), 3.0), "all equal percentile == 3");
    }
}

static void test_repeated_percentile_keys()
{
    std::vector<double> t{1,2,3,4,5};
    std::vector<int> pct{50,50,90};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(count_pct_key(ag.percentiles, 50) == 2, "keep duplicate pctl keys");
    assert_true(approx(get_pct_value(ag.percentiles, 50), 3.0), "p50 value (first occurrence) = 3");
    assert_true(approx(get_pct_value(ag.percentiles, 90), 5.0), "p90 value = 5");
}

static void test_empty_pctl_list()
{
    std::vector<double> t{9.0, 1.0};
    std::vector<int> pct{};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(ag.percentiles.empty(), "empty pctl -> empty output");
}

static void test_descending_input()
{
    std::vector<double> t{10.0, 8.0, 6.0, 4.0, 2.0}; // descending order
    std::vector<int> pct{0, 50, 100};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(ag.min, 2.0) && approx(ag.max, 10.0), "descending min/max");
    assert_true(approx(get_pct_value(ag.percentiles, 0), 2.0), "p0=2");
    assert_true(approx(get_pct_value(ag.percentiles, 50), 6.0), "p50=6");
    assert_true(approx(get_pct_value(ag.percentiles, 100), 10.0), "p100=10");
}

static void test_large_n_linear()
{
    std::vector<double> t;
    t.reserve(100);
    for (int i = 1; i <= 100; ++i) t.push_back(static_cast<double>(i));
    std::vector<int> pct{1, 50, 99, 100};
    Aggregation ag = aggregate_times(t, pct);
    assert_true(approx(get_pct_value(ag.percentiles, 1), 1.0), "p1=1");
    assert_true(approx(get_pct_value(ag.percentiles, 50), 50.0), "p50=50");
    assert_true(approx(get_pct_value(ag.percentiles, 99), 99.0), "p99=99");
    assert_true(approx(get_pct_value(ag.percentiles, 100), 100.0), "p100=100");
}

int main()
{
    test_empty();
    test_singleton();
    test_multiple_even();
    test_clamp();
    test_all_equal_values();
    test_repeated_percentile_keys();
    test_empty_pctl_list();
    test_descending_input();
    test_large_n_linear();
    std::cout << "aggregate tests: OK" << std::endl;
    return 0;
}
