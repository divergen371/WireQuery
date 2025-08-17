// DNS Resolver & Timing Tool (C++23)
// Build example (Homebrew Clang):
//   /opt/homebrew/opt/llvm/bin/clang++ -std=c++23 -stdlib=libc++ \
//     -I/opt/homebrew/opt/llvm/include/c++/v1 main.cpp -o main

#include <algorithm>
#include <chrono>
#include <mutex>
#include <numeric>
#include <print>     // std::print, std::println
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
// noinspection CppUnusedIncludeDirective
// NOLINTNEXTLINE
#include <cstdio>
// noinspection CppUnusedIncludeDirective
// NOLINTNEXTLINE
#include <cctype>
#include <format>
#include <iomanip>

// POSIX networking
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "wq/options.hpp"
#include "wq/model.hpp"
#include "wq/json.hpp"
#include "wq/cli.hpp"
#include "wq/output.hpp"
#include "wq/resolver.hpp"
#include "wq/rawdns.hpp"
#include "wq/usecases.hpp"

using namespace std::string_view_literals;
using wq::Family;
using wq::Options;
using wq::Entry;
using wq::PtrItem;
using wq::AttemptResult;
using wq::json_escape;
using wq::print_usage;
using wq::parse_args;
using wq::family_str;
using wq::socktype_str;
using wq::proto_str;

static std::mutex g_print_mtx;


// print_usage moved to wq/cli.hpp

// family_str/socktype_str/proto_str are provided by wq/output.hpp

// family_to_af moved into infra resolver

// moved to src/presentation/output_text.cpp

// --- Data structures moved to wq/model.hpp ---

// collect_entries moved into infra resolver

// do_reverse_for_entries moved into infra resolver
// parse_args moved to wq/cli.hpp

int main(int argc, char **argv)
{
    Options opt;
    if (argc <= 1)
    {
        print_usage(argv[0]);
        return 0;
    }
    if (!parse_args(argc, argv, opt))
    {
        if (opt.host.empty())
        {
            print_usage(argv[0]);
        }
        return 1;
    }

    if (!opt.json && !opt.ndjson)
    {
        std::print("{}", wq::format_header_text(opt));
    }

    std::vector<AttemptResult> attempts(opt.json ? opt.tries : 0);
    std::vector<double> times = wq::run_queries(
        opt,
        [&](int t,
            double ms,
            const AttemptResult *posix,
            const wq::RawDnsResult *rawdns)
        {
            if (rawdns)
            {
                const auto &rd = *rawdns;
                if (rd.rc != 0)
                {
                    if (opt.ndjson)
                    {
                        std::string out;
                        switch (rd.kind)
                        {
                            case wq::RawDnsErrorKind::NotAvailable:
                                out = wq::build_ndjson_ldns_not_available(
                                    t,
                                    ms,
                                    rd.error,
                                    opt);
                                break;
                            case wq::RawDnsErrorKind::InitFailed:
                                out = wq::build_ndjson_rawdns_init_failed(
                                    t,
                                    ms,
                                    rd.error,
                                    opt);
                                break;
                            case wq::RawDnsErrorKind::InvalidQname:
                            case wq::RawDnsErrorKind::QueryFailed:
                            default:
                                out = wq::build_ndjson_rawdns_error_with_type(
                                    t,
                                    ms,
                                    rd.error,
                                    opt.qtype);
                                break;
                        }
                        std::scoped_lock lk(g_print_mtx);
                        std::print("{}\n", out);
                    }
                    else if (opt.json)
                    {
                        AttemptResult ar{};
                        ar.ms = ms;
                        ar.rc = -1;
                        ar.error = rd.error;
                        attempts[t - 1] = std::move(ar);
                    }
                    else
                    {
                        std::scoped_lock lk(g_print_mtx);
                        std::println(
                            "try {}: {:.3f} ms - raw DNS error: {}",
                            t,
                            ms,
                            rd.error);
                    }
                    return;
                }

                if (opt.ndjson)
                {
                    std::string out = wq::build_ndjson_rawdns_success(
                        t,
                        ms,
                        opt.qtype,
                        rd.rcode,
                        rd.f_aa,
                        rd.f_tc,
                        rd.f_rd,
                        rd.f_ra,
                        rd.f_ad,
                        rd.f_cd,
                        rd.answer_count,
                        rd.authority_count,
                        rd.additional_count,
                        rd.answers);
                    std::scoped_lock lk(g_print_mtx);
                    std::print("{}\n", out);
                }
                else if (opt.json)
                {
                    AttemptResult ar{};
                    ar.ms = ms;
                    ar.rc = 0;
                    attempts[t - 1] = std::move(ar);
                }
                else
                {
                    std::scoped_lock lk(g_print_mtx);
                    std::println(
                        "try {}: {:.3f} ms - raw DNS rcode={} aa={} tc={} rd={} ra={} ad={} cd={} an={}",
                        t,
                        ms,
                        rd.rcode,
                        rd.f_aa,
                        rd.f_tc,
                        rd.f_rd,
                        rd.f_ra,
                        rd.f_ad,
                        rd.f_cd,
                        rd.answer_count);
                }
                return;
            }

            // POSIX path
            const auto &res = *posix;
            if (res.rc != 0)
            {
                if (opt.ndjson)
                {
                    std::string out = wq::build_ndjson_getaddrinfo_error(
                        t,
                        ms,
                        res.rc);
                    std::scoped_lock lk(g_print_mtx);
                    std::print("{}\n", out);
                }
                else if (opt.json)
                {
                    AttemptResult ar_out{};
                    ar_out.ms = ms;
                    ar_out.rc = res.rc;
                    ar_out.error = res.error;
                    attempts[t - 1] = std::move(ar_out);
                }
                else
                {
                    std::scoped_lock lk(g_print_mtx);
                    std::println(
                        "try {}: {:.3f} ms - error: {}",
                        t,
                        ms,
                        res.error);
                }
                return;
            }

            // Success
            if (opt.ndjson)
            {
                std::string out = wq::build_ndjson_normal(
                    t,
                    ms,
                    res.canon,
                    res.entries,
                    res.ptrs);
                std::scoped_lock lk(g_print_mtx);
                std::print("{}\n", out);
            }
            else if (opt.json)
            {
                AttemptResult ar{};
                ar.ms = ms;
                ar.rc = 0;
                ar.canon = res.canon;
                ar.entries = res.entries;
                ar.ptrs = res.ptrs;
                attempts[t - 1] = std::move(ar);
            }
            else
            {
                std::scoped_lock lk(g_print_mtx);
                std::print("{}", wq::format_entries_text(res.entries));
                std::print("{}", wq::format_ptrs_text(res.ptrs));
                std::print(
                    "{}",
                    wq::format_try_footer_text(
                        t,
                        ms,
                        res.entries.size(),
                        res.canon));
            }
        });

    if (!times.empty())
    {
        wq::Aggregation ag = wq::aggregate_times(times, opt.pctl);
        if (opt.json && !opt.ndjson)
        {
            std::string out = wq::build_final_json(
                opt,
                ag.min,
                ag.avg,
                ag.max,
                ag.percentiles,
                attempts);
            std::print("{}\n", out);
        }
        else if (!opt.ndjson)
        {
            std::print(
                "{}",
                wq::format_summary_text(ag.min, ag.avg, ag.max, times.size()));
            std::print("{}", wq::format_percentiles_text(ag.percentiles));
        }
    }

    return 0;
}
