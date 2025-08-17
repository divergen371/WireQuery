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

} // namespace wq
