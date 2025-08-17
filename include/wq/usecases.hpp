#pragma once

#include <vector>
#include <functional>
#include <utility>

#include "wq/options.hpp"
#include "wq/model.hpp"
#include "wq/rawdns.hpp"

namespace wq {

// per-try コールバック。posix または rawdns のどちらか一方のみが非 nullptr
using TryCallback = std::function<void(int /*try_index (1-based)*/,
                                       double /*ms*/,
                                       const AttemptResult* /*posix*/,
                                       const RawDnsResult* /*rawdns*/)>;

// 並行実行・複数トライを実行し、各トライごとに on_try を呼び出す。
// 返り値は、各トライの計測ミリ秒配列。
std::vector<double> run_queries(const Options& opt, const TryCallback& on_try);

struct Aggregation {
    double min{};
    double avg{};
    double max{};
    std::vector<std::pair<int,double>> percentiles; // (p, value)
};

// times と要求パーセンタイル pctl (0..100) から統計量を算出
Aggregation aggregate_times(const std::vector<double>& times, const std::vector<int>& pctl);

} // namespace wq
