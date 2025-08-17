#pragma once

#include <vector>
#include <functional>
#include <utility>

#include "wq/options.hpp"
#include "wq/model.hpp"
#include "wq/rawdns.hpp"
#include "wq/aggregate.hpp"

namespace wq {

// per-try コールバック。posix または rawdns のどちらか一方のみが非 nullptr
using TryCallback = std::function<void(int /*try_index (1-based)*/,
                                       double /*ms*/,
                                       const AttemptResult* /*posix*/,
                                       const RawDnsResult* /*rawdns*/)>;

// 並行実行・複数トライを実行し、各トライごとに on_try を呼び出す。
// 返り値は、各トライの計測ミリ秒配列。
std::vector<double> run_queries(const Options& opt, const TryCallback& on_try);

} // namespace wq
