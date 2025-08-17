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

int main()
{
    test_empty();
    test_singleton();
    test_multiple_even();
    test_clamp();
    std::cout << "aggregate tests: OK" << std::endl;
    return 0;
}
