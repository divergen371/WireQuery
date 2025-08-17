#include "wq/runner.hpp"

#include "wq/rawdns.hpp"
#include "wq/concurrency.hpp"

namespace wq
{
std::vector<double> run_rawdns_queries(const Options &opt,
                                       const TryCallback &on_try)
{
    std::vector<double> times;
    times.assign(opt.tries, 0.0);

    auto do_one = [&](int t)
    {
        RawDnsResult rd = resolve_rawdns_once(opt);
        times[t - 1] = rd.ms;
        if (on_try) on_try(t, rd.ms, nullptr, &rd);
    };

    for_each_index_batched(opt.tries, opt.concurrency, do_one);
    return times;
}
} // namespace wq
