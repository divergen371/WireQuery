#include "wq/runner.hpp"

#include "wq/resolver.hpp"
#include "wq/concurrency.hpp"

namespace wq
{
std::vector<double> run_posix_queries(const Options &opt,
                                      const TryCallback &on_try)
{
    std::vector<double> times;
    times.assign(opt.tries, 0.0);

    auto do_one = [&](int t)
    {
        AttemptResult ar = resolve_posix_once(opt);
        times[t - 1] = ar.ms;
        if (on_try) on_try(t, ar.ms, &ar, nullptr);
    };

    for_each_index_batched(opt.tries, opt.concurrency, do_one);
    return times;
}
} // namespace wq
