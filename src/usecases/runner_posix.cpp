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

    Cancellation cancel;
    auto do_one = [&](int t, const std::atomic<bool> &)
    {
        AttemptResult ar = resolve_posix_once(opt);
        times[t - 1] = ar.ms;
        if (on_try)
        {
            if (opt.stop_on_error)
            {
                // 例外は上位（for_each_index_batched_cancelable）で捕捉・キャンセル・再throw
                on_try(t, ar.ms, &ar, nullptr);
            }
            else
            {
                // 継続モードでは on_try 例外を抑止して全トライを実行
                try { on_try(t, ar.ms, &ar, nullptr); }
                catch (...) { /* swallow to continue all tries */ }
            }
        }
    };

    for_each_index_batched_cancelable(
        opt.tries, opt.concurrency, do_one, &cancel);
    return times;
}
} // namespace wq
