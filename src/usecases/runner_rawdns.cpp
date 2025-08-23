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

    Cancellation cancel;
    auto do_one = [&](int t, const std::atomic<bool> &)
    {
        RawDnsResult rd = resolve_rawdns_once(opt);
        times[t - 1] = rd.ms;
        if (on_try)
        {
            if (opt.stop_on_error)
            {
                on_try(t, rd.ms, nullptr, &rd);
            }
            else
            {
                try { on_try(t, rd.ms, nullptr, &rd); }
                catch (...) { /* swallow to continue all tries */ }
            }
        }
    };

    for_each_index_batched_cancelable(
        opt.tries, opt.concurrency, do_one, &cancel);
    return times;
}
} // namespace wq
