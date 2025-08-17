#include "wq/aggregate.hpp"

#include <algorithm>
#include <numeric>

namespace wq {

Aggregation aggregate_times(const std::vector<double>& times, const std::vector<int>& pctl)
{
    Aggregation ag{};
    if (times.empty()) return ag;

    auto [min_it, max_it] = std::minmax_element(times.begin(), times.end());
    ag.min = *min_it;
    ag.max = *max_it;
    ag.avg = std::accumulate(times.begin(), times.end(), 0.0) /
             static_cast<double>(times.size());

    std::vector<double> sorted = times;
    std::ranges::sort(sorted);
    auto pct_value = [&](int p) -> double
    {
        if (sorted.empty()) return 0.0;
        size_t n  = sorted.size();
        int    pc = std::clamp(p, 0, 100);
        size_t rank = (static_cast<size_t>(pc) * n + 100 - 1) / 100; // ceil
        if (rank < 1) rank = 1;
        if (rank > n) rank = n;
        return sorted[rank - 1];
    };

    ag.percentiles.reserve(pctl.size());
    for (int p : pctl) ag.percentiles.emplace_back(p, pct_value(p));
    return ag;
}

} // namespace wq
