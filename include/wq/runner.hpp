#pragma once

#include <vector>
#include "wq/usecases.hpp"

namespace wq {

// 戦略別ランナー（POSIX / raw DNS）
// それぞれが並行実行・複数トライを行い、on_try をコールします。
std::vector<double> run_posix_queries(const Options& opt, const TryCallback& on_try);
std::vector<double> run_rawdns_queries(const Options& opt, const TryCallback& on_try);

} // namespace wq
