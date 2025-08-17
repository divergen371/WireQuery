#pragma once

#include <functional>

namespace wq {

// Execute fn(index) for index = 1..total with at most `concurrency` threads at a time.
// If concurrency <= 1, runs sequentially.
// The function guarantees that all tasks complete before returning.
void for_each_index_batched(int total, int concurrency, const std::function<void(int)>& fn);

} // namespace wq
