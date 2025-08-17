#include "wq/cli.hpp"

#include <algorithm>
#include <cctype>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// POSIX constants for socktype/protocol
#include <sys/socket.h>
#include <netinet/in.h>

using namespace std::string_view_literals;

namespace wq {

void print_usage(const char *prog)
{
    std::println("DNS resolver / timing tool");
    std::println("Usage: {} [options] <hostname>", prog);
    std::println("Options:");
    std::println(
        "  --tries N          Number of resolution attempts (default: 3)");
    std::println(
        "  --concurrency K    Number of parallel lookups (default: 1)");
    std::println("  --parallel K       Alias of --concurrency");
    std::println(
        "  --family F         Address family: any|inet|inet6 (default: any)");
    std::println("  -4                 Shortcut for --family inet");
    std::println("  -6                 Shortcut for --family inet6");
    std::println("  --service S        Service name or port (e.g., 80, http)");
    std::println("  --socktype T       stream|dgram|raw (default: any)");
    std::println("  --protocol P       tcp|udp (default: any)");
    std::println("  --[no-]addrconfig  Toggle AI_ADDRCONFIG (default: on)");
    std::println("  --[no-]canonname   Toggle AI_CANONNAME (default: on)");
    std::println("  --all              AI_ALL (IPv6 + V4MAPPED only)");
    std::println("  --v4mapped         AI_V4MAPPED");
    std::println("  --numeric-host     AI_NUMERICHOST (no DNS query)");
    std::println("  --reverse          Do reverse (PTR) lookups for results");
    std::println("  --ptr              Alias of --reverse");
    std::println(
        "  --ni-namereqd      Use NI_NAMEREQD for reverse (require name)");
    std::println("  --json             Output results in JSON format");
    std::println(
        "  --ndjson           Output each attempt as a single JSON line (NDJSON)");
    std::println(
        "  --pctl LIST        Comma-separated percentiles for summary (e.g., 50,90,99)");
    std::println("  --dedup            Fold duplicate results per attempt");
    std::println(
        "  --type RR          Raw DNS mode (ldns): A,AAAA,CNAME,NS,MX,TXT,SOA,CAA,SRV,DS,DNSKEY,PTR");
    std::println("  --ns SERVER        DNS server to query (IP or host)");
    std::println("  --rd on|off        Recursion Desired flag (default: on)");
    std::println("  --do on|off        DNSSEC DO flag (default: off)");
    std::println(
        "  --timeout MS       Query timeout in milliseconds (default: 2000)");
    std::println(
        "  --tcp              Force TCP transport (default: UDP with TCP fallback)");
    std::println("  -h, --help         Show this help");
    std::println("");
    std::println("Examples:");
    std::println("  {} example.com", prog);
    std::println(
        "  {} --tries 5 --family inet6 --v4mapped --all www.google.com",
        prog);
}

bool parse_args(int argc, char **argv, Options &opt)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i];
        if (a == "-h"sv || a == "--help"sv)
        {
            print_usage(argv[0]);
            return false;
        }
        if (a == "-4"sv)
        {
            opt.family = Family::IPv4;
        }
        else if (a == "-6"sv)
        {
            opt.family = Family::IPv6;
        }
        else if (a == "--all"sv)
        {
            opt.all = true;
        }
        else if (a == "--v4mapped"sv)
        {
            opt.v4mapped = true;
        }
        else if (a == "--numeric-host"sv)
        {
            opt.numeric_host = true;
        }
        else if (a == "--addrconfig"sv)
        {
            opt.addrconfig = true;
        }
        else if (a == "--no-addrconfig"sv)
        {
            opt.addrconfig = false;
        }
        else if (a == "--canonname"sv)
        {
            opt.canonname = true;
        }
        else if (a == "--no-canonname"sv)
        {
            opt.canonname = false;
        }
        else if (a.rfind("--family", 0) == 0)
        {
            // starts_with
            std::string val;
            if (a == "--family"sv && i + 1 < argc)
            {
                val = argv[++i];
            }
            else if (a.size() > 9 && a.substr(8, 1) == "="sv)
            {
                val = std::string(a.substr(9));
            }
            else
            {
                std::println("invalid --family usage");
                return false;
            }
            if (val == "any") opt.family = Family::Any;
            else if (val == "inet") opt.family = Family::IPv4;
            else if (val == "inet6") opt.family = Family::IPv6;
            else
            {
                std::println("unknown family: {}", val);
                return false;
            }
        }
        else if (a.rfind("--service", 0) == 0)
        {
            if (a == "--service"sv && i + 1 < argc)
            {
                opt.service = argv[++i];
            }
            else if (a.size() > 10 && a.substr(9, 1) == "="sv)
            {
                opt.service = std::string(a.substr(10));
            }
            else
            {
                std::println("invalid --service usage");
                return false;
            }
        }
        else if (a.rfind("--socktype", 0) == 0)
        {
            std::string val;
            if (a == "--socktype"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 11 && a.substr(10, 1) ==
                     "="sv)
                val = std::string(a.substr(11));
            else
            {
                std::println("invalid --socktype usage");
                return false;
            }
            if (val == "stream") opt.socktype = SOCK_STREAM;
            else if (val == "dgram") opt.socktype = SOCK_DGRAM;
            else if (val == "raw") opt.socktype = SOCK_RAW;
            else if (val == "any") opt.socktype = 0;
            else
            {
                std::println("unknown socktype: {}", val);
                return false;
            }
        }
        else if (a.rfind("--protocol", 0) == 0)
        {
            std::string val;
            if (a == "--protocol"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 11 && a.substr(10, 1) ==
                     "="sv)
                val = std::string(a.substr(11));
            else
            {
                std::println("invalid --protocol usage");
                return false;
            }
            if (val == "tcp") opt.protocol = IPPROTO_TCP;
            else if (val == "udp") opt.protocol = IPPROTO_UDP;
            else if (val == "any") opt.protocol = 0;
            else
            {
                std::println("unknown protocol: {}", val);
                return false;
            }
        }
        else if (a == "--reverse"sv || a == "--ptr"sv)
        {
            opt.reverse = true;
        }
        else if (a == "--ni-namereqd"sv)
        {
            opt.ni_namereqd = true;
        }
        else if (a.rfind("--concurrency", 0) == 0 || a.rfind("--parallel", 0)
                 == 0)
        {
            std::string val;
            if ((a == "--concurrency"sv || a == "--parallel"sv) && i + 1 <
                argc)
            {
                val = argv[++i];
            }
            else if (a.rfind("--concurrency", 0) == 0 && a.size() > 14 && a.
                     substr(13, 1) == "="sv)
            {
                val = std::string(a.substr(14));
            }
            else if (a.rfind("--parallel", 0) == 0 && a.size() > 11 && a.
                     substr(10, 1) == "="sv)
            {
                val = std::string(a.substr(11));
            }
            else
            {
                std::println("invalid --concurrency/--parallel usage");
                return false;
            }
            try { opt.concurrency = std::stoi(val); }
            catch (...)
            {
                std::println("invalid concurrency: {}", val);
                return false;
            }
            if (opt.concurrency <= 0) opt.concurrency = 1;
        }
        else if (a == "--json"sv)
        {
            opt.json = true;
        }
        else if (a == "--dedup"sv)
        {
            opt.dedup = true;
        }
        else if (a == "--ndjson"sv)
        {
            opt.ndjson = true;
        }
        else if (a.rfind("--pctl", 0) == 0)
        {
            std::string val;
            if (a == "--pctl"sv && i + 1 < argc)
            {
                val = argv[++i];
            }
            else if (a.size() > 7 && a.substr(6, 1) == "="sv)
            {
                val = std::string(a.substr(7));
            }
            else
            {
                std::println("invalid --pctl usage");
                return false;
            }
            std::vector<int> out;
            std::string      num;
            for (char ch: val)
            {
                if (ch == ',')
                {
                    if (!num.empty())
                    {
                        try
                        {
                            int p = std::stoi(num);
                            if (p < 0 || p > 100)
                            {
                                std::println("percentile out of range: {}", p);
                                return false;
                            }
                            out.push_back(p);
                        }
                        catch (...)
                        {
                            std::println("invalid percentile: {}", num);
                            return false;
                        }
                        num.clear();
                    }
                }
                else if (ch >= '0' && ch <= '9')
                {
                    num.push_back(ch);
                }
                else
                {
                    std::println(
                        "invalid --pctl character: {}",
                        std::string(1, ch));
                    return false;
                }
            }
            if (!num.empty())
            {
                try
                {
                    int p = std::stoi(num);
                    if (p < 0 || p > 100)
                    {
                        std::println("percentile out of range: {}", p);
                        return false;
                    }
                    out.push_back(p);
                }
                catch (...)
                {
                    std::println("invalid percentile: {}", num);
                    return false;
                }
            }
            std::ranges::sort(out);
            out.erase(std::ranges::unique(out).begin(), out.end());
            opt.pctl = std::move(out);
        }
        else if (a.rfind("--tries", 0) == 0)
        {
            std::string val;
            if (a == "--tries"sv && i + 1 < argc)
            {
                val = argv[++i];
            }
            else if (a.size() > 8 && a.substr(7, 1) == "="sv)
            {
                val = std::string(a.substr(8));
            }
            else
            {
                std::println("invalid --tries usage");
                return false;
            }
            try { opt.tries = std::stoi(val); }
            catch (...)
            {
                std::println("invalid tries: {}", val);
                return false;
            }
            if (opt.tries <= 0) opt.tries = 1;
        }
        else if (a.rfind("--type", 0) == 0)
        {
            std::string val;
            if (a == "--type"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 7 && a.substr(6, 1) == "="sv)
                val = std::string(
                    a.substr(7));
            else
            {
                std::println("invalid --type usage");
                return false;
            }
            // Uppercase normalize
            std::ranges::transform(
                val,
                val.begin(),
                [](unsigned char c)
                {
                    return std::toupper(c);
                });
            opt.qtype = std::move(val);
        }
        else if (a.rfind("--ns", 0) == 0)
        {
            if (a == "--ns"sv && i + 1 < argc) opt.ns = argv[++i];
            else if (a.size() > 5 && a.substr(4, 1) ==
                     "="sv)
                opt.ns = std::string(a.substr(5));
            else
            {
                std::println("invalid --ns usage");
                return false;
            }
        }
        else if (a.rfind("--rd", 0) == 0)
        {
            std::string val;
            if (a == "--rd"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 5 && a.substr(4, 1) == "="sv)
                val = std::string(
                    a.substr(5));
            else
            {
                std::println("invalid --rd usage");
                return false;
            }
            if (val == "on" || val == "1" || val == "true") opt.rd = true;
            else if (val == "off" || val == "0" || val ==
                     "false")
                opt.rd = false;
            else
            {
                std::println("invalid --rd value: {}", val);
                return false;
            }
        }
        else if (a.rfind("--do", 0) == 0)
        {
            std::string val;
            if (a == "--do"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 5 && a.substr(4, 1) == "="sv)
                val = std::string(
                    a.substr(5));
            else
            {
                std::println("invalid --do usage");
                return false;
            }
            if (val == "on" || val == "1" || val == "true") opt.do_bit = true;
            else if (val == "off" || val == "0" || val ==
                     "false")
                opt.do_bit = false;
            else
            {
                std::println("invalid --do value: {}", val);
                return false;
            }
        }
        else if (a.rfind("--timeout", 0) == 0)
        {
            std::string val;
            if (a == "--timeout"sv && i + 1 < argc) val = argv[++i];
            else if (a.size() > 10 && a.substr(9, 1) ==
                     "="sv)
                val = std::string(a.substr(10));
            else
            {
                std::println("invalid --timeout usage");
                return false;
            }
            try { opt.timeout_ms = std::stoi(val); }
            catch (...)
            {
                std::println("invalid --timeout value: {}", val);
                return false;
            }
            if (opt.timeout_ms < 0) opt.timeout_ms = 0;
        }
        else if (a == "--tcp"sv)
        {
            opt.tcp = true;
        }
        else if (!a.empty() && a[0] == '-')
        {
            std::println("unknown option: {}", a);
            return false;
        }
        else
        {
            opt.host = std::string(a);
        }
    }
    if (opt.host.empty()) return false;
    return true;
}

} // namespace wq
