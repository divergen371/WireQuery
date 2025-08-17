#include "wq/usecases.hpp"

#include <algorithm>
#include <numeric>
#include <thread>

#include "wq/resolver.hpp"
#include "wq/rawdns.hpp"

namespace wq {

std::vector<double> run_queries(const Options& opt, const TryCallback& on_try)
{
    std::vector<double> times;
    times.assign(opt.tries, 0.0);

    auto do_one = [&](int t)
    {
        if (!opt.qtype.empty())
        {
            RawDnsResult rd = resolve_rawdns_once(opt);
            times[t - 1] = rd.ms;
            if (on_try) on_try(t, rd.ms, nullptr, &rd);
        }
        else
        {
            AttemptResult ar = resolve_posix_once(opt);
            times[t - 1] = ar.ms;
            if (on_try) on_try(t, ar.ms, &ar, nullptr);
        }
    };

    if (opt.concurrency <= 1)
    {
        for (int t = 1; t <= opt.tries; ++t) do_one(t);
    }
    else
    {
        int next = 1;
        while (next <= opt.tries)
        {
            int batch = std::min(opt.concurrency, opt.tries - (next - 1));
            std::vector<std::thread> threads;
            threads.reserve(batch);
            for (int i = 0; i < batch; ++i)
            {
                int attempt = next + i;
                threads.emplace_back([&, attempt] { do_one(attempt); });
            }
            for (auto& th : threads)
            {
                if (th.joinable()) th.join();
            }
            next += batch;
        }
    }

    return times;
}

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
    std::sort(sorted.begin(), sorted.end());
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
