#include "wq/usecases.hpp"
#include "wq/runner.hpp"

namespace wq
{
std::vector<double> run_queries(const Options &opt, const TryCallback &on_try)
{
    if (!opt.qtype.empty())
    {
        return run_rawdns_queries(opt, on_try);
    }
    return run_posix_queries(opt, on_try);
}
} // namespace wq
