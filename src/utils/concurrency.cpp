#include "wq/concurrency.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace wq {

void for_each_index_batched(int total, int concurrency, const std::function<void(int)>& fn)
{
    if (total <= 0) return;

    if (concurrency <= 1)
    {
        for (int i = 1; i <= total; ++i) fn(i);
        return;
    }

    int next = 1;
    while (next <= total)
    {
        const int batch = std::min(concurrency, total - (next - 1));
        std::vector<std::thread> threads;
        threads.reserve(batch);
        for (int i = 0; i < batch; ++i)
        {
            const int index = next + i;
            threads.emplace_back([&, index] { fn(index); });
        }
        for (auto& th : threads)
        {
            if (th.joinable()) th.join();
        }
        next += batch;
    }
}

} // namespace wq
