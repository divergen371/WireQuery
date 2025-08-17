#pragma once

#include <vector>
#include <utility>

namespace wq {

struct Aggregation {
    double min{};
    double avg{};
    double max{};
    std::vector<std::pair<int,double>> percentiles; // (p, value)
};

// times と要求パーセンタイル pctl (0..100) から統計量を算出
Aggregation aggregate_times(const std::vector<double>& times, const std::vector<int>& pctl);

} // namespace wq
