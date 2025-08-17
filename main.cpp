// DNS Resolver & Timing Tool (C++23)
// Build example (Homebrew Clang):
//   /opt/homebrew/opt/llvm/bin/clang++ -std=c++23 -stdlib=libc++ \
//     -I/opt/homebrew/opt/llvm/include/c++/v1 main.cpp -o main

#include <print>     // std::print, std::println
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <sstream>
// noinspection CppUnusedIncludeDirective
// NOLINTNEXTLINE
#include <cstdio>
// noinspection CppUnusedIncludeDirective
// NOLINTNEXTLINE
#include <cctype>
#include <iomanip>
#include <format>

// POSIX networking
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

using namespace std::string_view_literals;

static std::mutex g_print_mtx;

#ifdef HAVE_LDNS
#include <ldns/ldns.h>
#endif

enum class Family { Any, IPv4, IPv6 };

struct Options
{
    std::string host;
    int         tries  = 3;
    Family      family = Family::Any;
    // detailed controls
    bool             addrconfig   = true;  // AI_ADDRCONFIG
    bool             canonname    = true;  // AI_CANONNAME
    bool             all          = false; // AI_ALL
    bool             v4mapped     = false; // AI_V4MAPPED
    bool             numeric_host = false; // AI_NUMERICHOST
    int              socktype     = 0;     // 0=any, SOCK_STREAM/DGRAM/RAW
    int              protocol     = 0;     // 0=any, IPPROTO_TCP/UDP
    std::string      service;              // service name or port (optional)
    bool             reverse     = false;  // perform PTR lookups
    bool             ni_namereqd = false;  // NI_NAMEREQD for getnameinfo
    int              concurrency = 1;      // number of parallel attempts
    bool             json        = false;  // JSON output mode
    bool             dedup       = false;  // fold duplicate results
    bool             ndjson      = false;  // NDJSON streaming per attempt
    std::vector<int> pctl;                 // requested percentiles (0..100)
    // Raw DNS (ldns) controls - Phase1
    std::string qtype;
    // when non-empty, enable raw DNS path (e.g., "A","AAAA","TXT",...)
    std::string ns;                 // server IP/host (authoritative/recursive)
    bool        rd         = true;  // recursion desired bit
    bool        do_bit     = false; // DNSSEC DO bit in EDNS
    int         timeout_ms = 2000;  // per-attempt timeout
    bool        tcp        = false; // force TCP transport
};

static void print_usage(const char *prog)
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

static const char *family_str(int af);


[[maybe_unused]] static void reverse_lookup_list(addrinfo *res, bool namereqd)
{
    char name[NI_MAXHOST]{};
    char ipbuf[INET6_ADDRSTRLEN]{};
    for (addrinfo *ai = res; ai != nullptr; ai = ai->ai_next)
    {
        // Prepare numeric address for context
        std::string ip;
        if (ai->ai_family == AF_INET)
        {
            const auto *sin = reinterpret_cast<const sockaddr_in *>(ai->
                ai_addr);
            if (inet_ntop(
                AF_INET,
                &sin->sin_addr,
                ipbuf,
                sizeof(ipbuf)))
                ip = ipbuf;
        }
        else if (ai->ai_family == AF_INET6)
        {
            const auto *sin6 = reinterpret_cast<const sockaddr_in6 *>(ai->
                ai_addr);
            if (inet_ntop(
                AF_INET6,
                &sin6->sin6_addr,
                ipbuf,
                sizeof(ipbuf)))
                ip = ipbuf;
        }

        int flags = NI_NOFQDN;
        if (namereqd) flags |= NI_NAMEREQD;
        int rc = getnameinfo(
            ai->ai_addr,
            ai->ai_addrlen,
            name,
            sizeof(name),
            nullptr,
            0,
            flags);
        if (rc == 0)
        {
            std::println(
                "  PTR: [{}] {} -> {}",
                family_str(ai->ai_family),
                ip.empty() ? "-" : ip.c_str(),
                std::string_view{name});
        }
        else
        {
            std::println(
                "  PTR: [{}] {} -> <{}>",
                family_str(ai->ai_family),
                ip.empty() ? "-" : ip.c_str(),
                gai_strerror(rc));
        }
    }
}

static int family_to_af(Family f)
{
    switch (f)
    {
        case Family::IPv4: return AF_INET;
        case Family::IPv6: return AF_INET6;
        default: return AF_UNSPEC;
    }
}

static const char *family_str(int af)
{
    switch (af)
    {
        case AF_INET: return "inet";
        case AF_INET6: return "inet6";
        default: return "unspec";
    }
}

static const char *socktype_str(int st)
{
    switch (st)
    {
        case 0: return "any";
        case SOCK_STREAM: return "stream";
        case SOCK_DGRAM: return "dgram";
        case SOCK_RAW: return "raw";
        default: return "other";
    }
}

static const char *proto_str(int p)
{
    switch (p)
    {
        case 0: return "any";
        case IPPROTO_TCP: return "tcp";
        case IPPROTO_UDP: return "udp";
        default: return "other";
    }
}

// --- Data structures and helpers for dedup, reverse outside lock, and JSON ---
struct Entry
{
    int         af{};
    int         socktype{};
    int         protocol{};
    uint16_t    port{};
    std::string ip;
};

struct PtrItem
{
    int         af{};
    std::string ip;
    int         rc{};  // 0 if ok
    std::string name;  // valid if rc==0
    std::string error; // valid if rc!=0
};

struct AttemptResult
{
    double               ms{};
    int                  rc{};  // getaddrinfo rc
    std::string          error; // if rc!=0
    std::string          canon;
    std::vector<Entry>   entries;
    std::vector<PtrItem> ptrs; // may be empty when reverse disabled
};

static std::vector<Entry> collect_entries(addrinfo *res, bool dedup)
{
    std::vector<Entry>              out;
    std::unordered_set<std::string> seen;
    char                            buf[INET6_ADDRSTRLEN]{};
    for (addrinfo *ai = res; ai != nullptr; ai = ai->ai_next)
    {
        Entry e{};
        e.af       = ai->ai_family;
        e.socktype = ai->ai_socktype;
        e.protocol = ai->ai_protocol;
        if (ai->ai_family == AF_INET)
        {
            const auto *sin = reinterpret_cast<const sockaddr_in *>(ai->
                ai_addr);
            if (inet_ntop(
                AF_INET,
                &sin->sin_addr,
                buf,
                sizeof(buf)))
                e.ip = buf;
            e.port = ntohs(sin->sin_port);
        }
        else if (ai->ai_family == AF_INET6)
        {
            const auto *sin6 = reinterpret_cast<const sockaddr_in6 *>(ai->
                ai_addr);
            if (inet_ntop(
                AF_INET6,
                &sin6->sin6_addr,
                buf,
                sizeof(buf)))
                e.ip = buf;
            e.port = ntohs(sin6->sin6_port);
        }
        else
        {
            continue;
        }
        if (e.ip.empty()) continue;
        if (dedup)
        {
            std::string key = std::to_string(e.af) + '|' + e.ip + '|' +
                              std::to_string(e.socktype) + '|' +
                              std::to_string(e.protocol) + '|' + std::to_string(
                                  e.port);
            if (!seen.insert(key).second) continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

static std::vector<PtrItem> do_reverse_for_entries(
    const std::vector<Entry> &entries,
    bool                      namereqd)
{
    std::vector<PtrItem>            out;
    std::unordered_set<std::string> seen; // key: af|ip
    char                            name[NI_MAXHOST]{};
    for (const auto &e: entries)
    {
        std::string key = std::to_string(e.af) + '|' + e.ip;
        if (!seen.insert(key).second) continue;
        PtrItem item{};
        item.af = e.af;
        item.ip = e.ip;
        if (e.af == AF_INET)
        {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port   = htons(e.port);
            inet_pton(AF_INET, e.ip.c_str(), &sin.sin_addr);
            int flags = NI_NOFQDN | (namereqd ? NI_NAMEREQD : 0);
            int rc    = getnameinfo(
                reinterpret_cast<sockaddr *>(&sin),
                sizeof(sin),
                name,
                sizeof(name),
                nullptr,
                0,
                flags);
            item.rc = rc;
            if (rc == 0) item.name = std::string{name};
            else item.error        = gai_strerror(rc);
        }
        else if (e.af == AF_INET6)
        {
            sockaddr_in6 sin6{};
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port   = htons(e.port);
            inet_pton(AF_INET6, e.ip.c_str(), &sin6.sin6_addr);
            int flags = NI_NOFQDN | (namereqd ? NI_NAMEREQD : 0);
            int rc    = getnameinfo(
                reinterpret_cast<sockaddr *>(&sin6),
                sizeof(sin6),
                name,
                sizeof(name),
                nullptr,
                0,
                flags);
            item.rc = rc;
            if (rc == 0) item.name = std::string{name};
            else item.error        = gai_strerror(rc);
        }
        out.push_back(std::move(item));
    }
    return out;
}

static void print_entries(const std::vector<Entry> &entries)
{
    for (const auto &e: entries)
    {
        if (e.port)
            std::println(
                "  - [{}] {}  socktype={}  proto={}  port={}",
                family_str(e.af),
                e.ip,
                socktype_str(e.socktype),
                proto_str(e.protocol),
                e.port);
        else
            std::println(
                "  - [{}] {}  socktype={}  proto={}",
                family_str(e.af),
                e.ip,
                socktype_str(e.socktype),
                proto_str(e.protocol));
    }
}

static void print_ptrs(const std::vector<PtrItem> &ptrs)
{
    for (const auto &p: ptrs)
    {
        if (p.rc == 0)
            std::println(
                "  PTR: [{}] {} -> {}",
                family_str(p.af),
                p.ip,
                p.name);
        else
            std::println(
                "  PTR: [{}] {} -> <{}>",
                family_str(p.af),
                p.ip,
                p.error);
    }
}

static std::string json_escape(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char uc: s)
    {
        switch (char c = static_cast<char>(uc))
        {
            case '"': out += "\\\"";
                break;
            case '\\': out += "\\\\";
                break;
            case '\b': out += "\\b";
                break;
            case '\f': out += "\\f";
                break;
            case '\n': out += "\\n";
                break;
            case '\r': out += "\\r";
                break;
            case '\t': out += "\\t";
                break;
            default:
                if (uc < 0x20)
                {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
                    out += buf;
                }
                else out += c;
        }
    }
    return out;
}

static bool parse_args(int argc, char **argv, Options &opt)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i];
        if (a == "-h"sv || a == "--help"sv)
        {
            print_usage(argv[0]);
            return false;
        }
        else if (a == "-4"sv)
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
            else if ((a.rfind("--concurrency", 0) == 0 && a.size() > 14 && a.
                      substr(13, 1) == "="sv))
            {
                val = std::string(a.substr(14));
            }
            else if ((a.rfind("--parallel", 0) == 0 && a.size() > 11 && a.
                      substr(10, 1) == "="sv))
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
                else if ((ch >= '0' && ch <= '9'))
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
        std::println("Resolving: {}", opt.host);
        std::println(
            "Family: {}  Tries: {}",
            (opt.family == Family::Any
                 ? "any"
                 : opt.family == Family::IPv4
                       ? "inet"
                       : "inet6"),
            opt.tries);
        std::println(
            "Flags: addrconfig={} canonname={} all={} v4mapped={} numeric-host={}",
            opt.addrconfig ? "on" : "off",
            opt.canonname ? "on" : "off",
            opt.all ? "on" : "off",
            opt.v4mapped ? "on" : "off",
            opt.numeric_host ? "on" : "off");
        std::println(
            "Socktype: {}  Protocol: {}  Service: {}",
            socktype_str(opt.socktype),
            proto_str(opt.protocol),
            opt.service.empty() ? "(none)" : opt.service.c_str());
        std::println(
            "Reverse: {}  NI_NAMEREQD: {}  Concurrency: {}  JSON: {}  Dedup: {}",
            opt.reverse ? "on" : "off",
            opt.ni_namereqd ? "on" : "off",
            opt.concurrency,
            opt.json ? "on" : "off",
            opt.dedup ? "on" : "off");
        if (!opt.qtype.empty())
        {
            std::println(
                "Raw DNS: type={} ns={} rd={} do={} timeout_ms={} tcp={}",
                opt.qtype,
                opt.ns.empty() ? "(system)" : opt.ns.c_str(),
                opt.rd ? "on" : "off",
                opt.do_bit ? "on" : "off",
                opt.timeout_ms,
                opt.tcp ? "on" : "off");
        }
    }

    std::vector<double> times;
    times.assign(opt.tries, 0);
    std::vector<AttemptResult> attempts(opt.json ? opt.tries : 0);

    auto attempt_fn = [&](int t)
    {
        // Raw DNS path: if --type is specified, use ldns when available
        if (!opt.qtype.empty())
        {
            auto                 t0 = std::chrono::steady_clock::now();
            double               ms = 0.0;
            [[maybe_unused]] int rc = -1;

#ifdef HAVE_LDNS
            ldns_resolver *res = nullptr;
            ldns_status    st  = LDNS_STATUS_OK;
            // Build resolver either from resolv.conf or custom ns
            if (opt.ns.empty())
            {
                st = ldns_resolver_new_frm_file(&res, nullptr);
            }
            else
            {
                res = ldns_resolver_new();
                if (res)
                {
                    // push nameserver from string (A/AAAA only)
                    ldns_rdf *ns_rdf = nullptr;
                    // Try IPv6 first if ':' present
                    if (opt.ns.find(':') != std::string::npos)
                    {
                        ns_rdf = ldns_rdf_new_frm_str(
                            LDNS_RDF_TYPE_AAAA,
                            opt.ns.c_str());
                    }
                    else
                    {
                        ns_rdf = ldns_rdf_new_frm_str(
                            LDNS_RDF_TYPE_A,
                            opt.ns.c_str());
                    }
                    if (ns_rdf)
                    {
                        (void) ldns_resolver_push_nameserver(res, ns_rdf);
                        ldns_rdf_deep_free(ns_rdf);
                    }
                    else
                    {
                        // cannot parse nameserver string
                        st = LDNS_STATUS_SYNTAX_RDATA_ERR;
                    }
                }
                else
                {
                    st = LDNS_STATUS_MEM_ERR;
                }
            }
            if (st != LDNS_STATUS_OK || !res)
            {
                auto t1e = std::chrono::steady_clock::now();
                ms       = std::chrono::duration<double, std::milli>(t1e - t0).
                        count();
                times[t - 1]    = ms;
                std::string err = "ldns_resolver init failed";
                if (opt.ndjson)
                {
                    std::ostringstream os;
                    os << std::fixed << std::setprecision(3);
                    os << "{";
                    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
                    os << R"(,"error":")" << json_escape(err) << R"(")";
                    os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype) <<
                            R"(","ns":")" << json_escape(opt.ns)
                            << R"(","rd":)" << (opt.rd ? "true" : "false") <<
                            R"(,"do":)" << (opt.do_bit ? "true" : "false")
                            << R"(,"timeout_ms":)" << opt.timeout_ms <<
                            R"(,"tcp":)" << (opt.tcp ? "true" : "false") << "}";
                    os << "}";
                    std::scoped_lock lk(g_print_mtx);
                    std::print("{}\n", os.str());
                }
                else if (opt.json)
                {
                    AttemptResult ar{};
                    ar.ms           = ms;
                    ar.rc           = -1;
                    ar.error        = std::move(err);
                    attempts[t - 1] = std::move(ar);
                }
                else
                {
                    std::scoped_lock lk(g_print_mtx);
                    std::println(
                        "try {}: {:.3f} ms - raw DNS error: {}",
                        t,
                        ms,
                        err);
                }
                if (res) ldns_resolver_deep_free(res);
                return;
            }

            // Apply resolver settings
            ldns_resolver_set_recursive(res, opt.rd);
            ldns_resolver_set_usevc(res, opt.tcp);
            ldns_resolver_set_fallback(res, true); // UDP->TCP fallback
            if (opt.timeout_ms >= 0)
            {
                struct timeval tv{
                    .tv_sec = opt.timeout_ms / 1000,
                    .tv_usec = (opt.timeout_ms % 1000) * 1000
                };
                ldns_resolver_set_timeout(res, tv);
            }
            // Prefer safe EDNS UDP size
            ldns_resolver_set_edns_udp_size(res, 1232);
            // Toggle the DO bit
            ldns_resolver_set_dnssec(res, opt.do_bit);

            // Build qname and type
            ldns_rdf *name = ldns_dname_new_frm_str(opt.host.c_str());
            if (!name)
            {
                auto t1e = std::chrono::steady_clock::now();
                ms       = std::chrono::duration<double, std::milli>(t1e - t0).
                        count();
                times[t - 1]    = ms;
                std::string err = "invalid qname";
                if (opt.ndjson)
                {
                    std::ostringstream os;
                    os << std::fixed << std::setprecision(3);
                    os << "{";
                    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
                    os << R"(,"error":")" << json_escape(err) << R"(")";
                    os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype) <<
                            R"("}})";
                    std::scoped_lock lk(g_print_mtx);
                    std::print("{}\n", os.str());
                }
                else if (opt.json)
                {
                    AttemptResult ar{};
                    ar.ms           = ms;
                    ar.rc           = -1;
                    ar.error        = std::move(err);
                    attempts[t - 1] = std::move(ar);
                }
                else
                {
                    std::scoped_lock lk(g_print_mtx);
                    std::println(
                        "try {}: {:.3f} ms - raw DNS error: invalid qname",
                        t,
                        ms);
                }
                ldns_resolver_deep_free(res);
                return;
            }

            ldns_rr_type qtype = ldns_get_rr_type_by_name(opt.qtype.c_str());
            if (qtype == 0)
            {
                // fallback for unknown string
                // minimal mapping for common types
                static const std::pair<std::string_view, ldns_rr_type> kTypeMap
                        [] = {
                            {"A", LDNS_RR_TYPE_A},
                            {"AAAA", LDNS_RR_TYPE_AAAA},
                            {"CNAME", LDNS_RR_TYPE_CNAME},
                            {"NS", LDNS_RR_TYPE_NS},
                            {"MX", LDNS_RR_TYPE_MX},
                            {"TXT", LDNS_RR_TYPE_TXT},
                            {"SOA", LDNS_RR_TYPE_SOA},
                            {"CAA", LDNS_RR_TYPE_CAA},
                            {"SRV", LDNS_RR_TYPE_SRV},
                            {"DS", LDNS_RR_TYPE_DS},
                            {"DNSKEY", LDNS_RR_TYPE_DNSKEY},
                            {"PTR", LDNS_RR_TYPE_PTR},
                        };
                for (const auto &kv: kTypeMap)
                {
                    if (opt.qtype == kv.first)
                    {
                        qtype = kv.second;
                        break;
                    }
                }
                if (qtype == 0) qtype = LDNS_RR_TYPE_A;
            }

            ldns_pkt *pkt    = nullptr;
            uint16_t  qflags = 0;
            if (opt.rd) qflags |= LDNS_RD;
            st = ldns_resolver_query_status(
                &pkt,
                res,
                name,
                qtype,
                LDNS_RR_CLASS_IN,
                qflags);
            auto t1 = std::chrono::steady_clock::now();
            ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            times[t - 1] = ms;

            if (st != LDNS_STATUS_OK || !pkt)
            {
                std::string err = "ldns query failed";
                if (opt.ndjson)
                {
                    std::ostringstream os;
                    os << std::fixed << std::setprecision(3);
                    os << "{";
                    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
                    os << R"(,"error":")" << json_escape(err) << R"(")";
                    os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype) <<
                            R"("}})";
                    std::scoped_lock lk(g_print_mtx);
                    std::print("{}\n", os.str());
                }
                else if (opt.json)
                {
                    AttemptResult ar{};
                    ar.ms           = ms;
                    ar.rc           = -1;
                    ar.error        = std::move(err);
                    attempts[t - 1] = std::move(ar);
                }
                else
                {
                    std::scoped_lock lk(g_print_mtx);
                    std::println(
                        "try {}: {:.3f} ms - raw DNS error: ldns query failed",
                        t,
                        ms);
                }
                if (pkt) ldns_pkt_free(pkt);
                ldns_rdf_deep_free(name);
                ldns_resolver_deep_free(res);
                return;
            }

            // Extract response details
            int  rcode = (int) ldns_pkt_get_rcode(pkt);
            bool f_aa  = ldns_pkt_aa(pkt);
            bool f_tc  = ldns_pkt_tc(pkt);
            bool f_rd  = ldns_pkt_rd(pkt);
            bool f_ra  = ldns_pkt_ra(pkt);
            bool f_ad  = ldns_pkt_ad(pkt);
            bool f_cd  = ldns_pkt_cd(pkt);

            ldns_rr_list *ans  = ldns_pkt_answer(pkt);
            ldns_rr_list *auth = ldns_pkt_authority(pkt);
            ldns_rr_list *addl = ldns_pkt_additional(pkt);
            size_t        an   = ans ? ldns_rr_list_rr_count(ans) : 0;
            size_t        au   = auth ? ldns_rr_list_rr_count(auth) : 0;
            size_t        ad   = addl ? ldns_rr_list_rr_count(addl) : 0;

            if (opt.ndjson)
            {
                std::ostringstream os;
                os << std::fixed << std::setprecision(3);
                os << "{";
                os << "\"try\":" << t << ",\"ms\":" << std::format("{:.3f}", ms)
                        << ",\"rc\":0";
                os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype) <<
                        R"(","rcode":)" << rcode
                        << R"(,"flags":{"aa":)" << (f_aa ? "true" : "false")
                        << R"(,"tc":)" << (f_tc ? "true" : "false")
                        << R"(,"rd":)" << (f_rd ? "true" : "false")
                        << R"(,"ra":)" << (f_ra ? "true" : "false")
                        << R"(,"ad":)" << (f_ad ? "true" : "false")
                        << R"(,"cd":)" << (f_cd ? "true" : "false") << "}"
                        << R"(,"counts":{"answer":)" << an << R"(,"authority":)"
                        << au << R"(,"additional":)" << ad << "}";
                // answers array as rr strings
                os << ",\"answers\":[";
                for (size_t i = 0; i < an; ++i)
                {
                    if (i) os << ",";
                    ldns_rr *rr = ldns_rr_list_rr(ans, i);
                    if (char *s = ldns_rr2str(rr))
                    {
                        os << R"(")" << json_escape(std::string{s}).c_str() <<
                                R"(")";
                        LDNS_FREE(s);
                    }
                    else { os << R"(")" << "" << R"(")"; }
                }
                os << "]}"; // close raw_dns and object
                std::scoped_lock lk(g_print_mtx);
                std::print("{}\n", os.str());
            }
            else if (opt.json)
            {
                AttemptResult ar{};
                ar.ms = ms;
                ar.rc = 0;
                ar.error.clear();
                attempts[t - 1] = std::move(ar);
            }
            else
            {
                std::scoped_lock lk(g_print_mtx);
                std::println(
                    "try {}: {:.3f} ms - raw DNS rcode={} aa={} tc={} rd={} ra={} ad={} cd={} an={}",
                    t,
                    ms,
                    rcode,
                    f_aa,
                    f_tc,
                    f_rd,
                    f_ra,
                    f_ad,
                    f_cd,
                    an);
            }

            if (pkt) ldns_pkt_free(pkt);
            ldns_rdf_deep_free(name);
            ldns_resolver_deep_free(res);
            return;
#else
            auto t1 = std::chrono::steady_clock::now();
            ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            times[t - 1] = ms;
            std::string err =
                    "ldns not available: rebuild with ldns (pkg-config ldns) to enable raw DNS";
            if (opt.ndjson)
            {
                std::ostringstream os;
                os << std::fixed << std::setprecision(3);
                os << "{";
                os << "\"try\":" << t << ",\"ms\":" << std::format("{:.3f}", ms)
                        << ",\"rc\":-1";
                os << R"(,"error":")" << json_escape(err) << R"(")";
                os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype) <<
                        R"(","ns":")" << json_escape(opt.ns)
                        << R"(","rd":)" << (opt.rd ? "true" : "false") <<
                        R"(,"do":)" << (opt.do_bit ? "true" : "false")
                        << R"(,"timeout_ms":)" << opt.timeout_ms << R"(,"tcp":)"
                        << (opt.tcp ? "true" : "false") << "}";
                os << "}";
                std::scoped_lock lk(g_print_mtx);
                std::print("{}\n", os.str());
            }
            else if (opt.json)
            {
                AttemptResult ar{};
                ar.ms           = ms;
                ar.rc           = -1;
                ar.error        = std::move(err);
                attempts[t - 1] = std::move(ar);
            }
            else
            {
                std::scoped_lock lk(g_print_mtx);
                std::println(
                    "try {}: {:.3f} ms - raw DNS error: {}",
                    t,
                    ms,
                    err);
            }
            return;
#endif
        }

        addrinfo hints{};
        hints.ai_family   = family_to_af(opt.family);
        hints.ai_socktype = opt.socktype; // 0 = any
        hints.ai_protocol = opt.protocol; // 0 = any
        hints.ai_flags    = 0;
        if (opt.addrconfig) hints.ai_flags |= AI_ADDRCONFIG;
        if (opt.canonname) hints.ai_flags |= AI_CANONNAME;
        if (opt.all) hints.ai_flags |= AI_ALL;
        if (opt.v4mapped) hints.ai_flags |= AI_V4MAPPED;
        if (opt.numeric_host) hints.ai_flags |= AI_NUMERICHOST;

        addrinfo *  res     = nullptr;
        auto        t0      = std::chrono::steady_clock::now();
        const char *service = opt.service.empty()
                                  ? nullptr
                                  : opt.service.c_str();
        int    rc = getaddrinfo(opt.host.c_str(), service, &hints, &res);
        auto   t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        times[t - 1] = ms;

        if (rc != 0)
        {
            if (opt.ndjson)
            {
                std::ostringstream os;
                os << std::fixed << std::setprecision(3);
                os << "{";
                os << "\"try\":" << t << ",\"ms\":" << std::format("{:.3f}", ms)
                        << ",\"rc\":"
                        << rc;
                os << R"(,"error":")" << json_escape(gai_strerror(rc)) << "\"";
                os << "}";
                std::scoped_lock lk(g_print_mtx);
                std::print("{}\n", os.str());
            }
            else if (opt.json)
            {
                AttemptResult ar{};
                ar.ms           = ms;
                ar.rc           = rc;
                ar.error        = gai_strerror(rc);
                attempts[t - 1] = std::move(ar);
            }
            else
            {
                std::scoped_lock lk(g_print_mtx);
                std::println(
                    "try {}: {:.3f} ms - error: {}",
                    t,
                    ms,
                    gai_strerror(rc));
            }
            if (res) freeaddrinfo(res);
            return;
        }

        // Build entries (with optional dedup) and reverse outside lock
        std::vector<Entry>   entries = collect_entries(res, opt.dedup);
        std::vector<PtrItem> ptrs;
        if (opt.reverse)
            ptrs = do_reverse_for_entries(
                entries,
                opt.ni_namereqd);
        std::string canon = (res && res->ai_canonname)
                                ? std::string(res->ai_canonname)
                                : std::string();

        if (opt.ndjson)
        {
            std::ostringstream os;
            os << std::fixed << std::setprecision(3);
            os << "{";
            os << "\"try\":" << t << ",\"ms\":" << std::format("{:.3f}", ms) <<
                    ",\"rc\":0";
            if (!canon.empty())
                os << R"(,"canon":")" << json_escape(canon) <<
                        "\"";
            os << ",\"addresses\":[";
            for (size_t j = 0; j < entries.size(); ++j)
            {
                const auto &e = entries[j];
                if (j) os << ",";
                os << R"({"family":")" << json_escape(family_str(e.af))
                        << R"(","ip":")" << json_escape(e.ip)
                        << R"(","socktype":")" << json_escape(
                            socktype_str(e.socktype))
                        << R"(","protocol":")" << json_escape(
                            proto_str(e.protocol))
                        << R"(","port":)" << e.port << "}";
            }
            os << "]";
            if (!ptrs.empty())
            {
                os << ",\"ptr\":[";
                for (size_t k = 0; k < ptrs.size(); ++k)
                {
                    const auto &p = ptrs[k];
                    if (k) os << ",";
                    os << R"({"family":")" << json_escape(family_str(p.af))
                            << R"(","ip":")" << json_escape(p.ip)
                            << R"(","rc":)" << p.rc;
                    if (p.rc == 0)
                        os << R"(,"name":")" << json_escape(p.name)
                                << "\"";
                    else os << R"(,"error":")" << json_escape(p.error) << "\"";
                    os << "}";
                }
                os << "]";
            }
            os << "}";
            std::scoped_lock lk(g_print_mtx);
            std::print("{}\n", os.str());
        }
        else if (opt.json)
        {
            AttemptResult ar{};
            ar.ms           = ms;
            ar.rc           = rc;
            ar.canon        = std::move(canon);
            ar.entries      = std::move(entries);
            ar.ptrs         = std::move(ptrs);
            attempts[t - 1] = std::move(ar);
        }
        else
        {
            std::scoped_lock lk(g_print_mtx);
            print_entries(entries);
            print_ptrs(ptrs);
            std::println(
                "try {}: {:.3f} ms - {} address(es)",
                t,
                ms,
                entries.size());
            if (!canon.empty()) std::println("  canon: {}", canon);
        }
        if (res) freeaddrinfo(res);
    };

    if (opt.concurrency <= 1)
    {
        for (int t = 1; t <= opt.tries; ++t) attempt_fn(t);
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
                threads.emplace_back([&, attempt] { attempt_fn(attempt); });
            }
            for (auto &th: threads)
            {
                if (th.joinable()) th.join();
            }
            next += batch;
        }
    }

    if (!times.empty())
    {
        auto [min_it, max_it] = std::minmax_element(times.begin(), times.end());
        double minv = *min_it, maxv = *max_it;
        double avg = std::accumulate(times.begin(), times.end(), 0.0) /
                     static_cast<double>(times.size());
        // Precompute percentiles if requested
        std::vector<double> sorted = times;
        std::ranges::sort(sorted);
        auto pct_value = [&](int p) -> double
        {
            if (sorted.empty()) return 0;
            size_t n  = sorted.size();
            int    pc = std::clamp(p, 0, 100);
            // ceil((pc/100.0)*n) を整数演算で: (pc * n + 99) / 100
            size_t rank = (static_cast<size_t>(pc) * n + 100 - 1) / 100;
            if (rank < 1) rank = 1;
            if (rank > n) rank = n;
            return sorted[rank - 1];
        };
        if (opt.json && !opt.ndjson)
        {
            // Emit JSON once at the end
            std::ostringstream os;
            os << std::fixed << std::setprecision(3);
            os << "{";
            os << R"("host":")" << json_escape(opt.host) << "\",";
            os << R"("family":")" << (opt.family == Family::Any
                                          ? "any"
                                          : opt.family == Family::IPv4
                                                ? "inet"
                                                : "inet6") << "\",";
            os << "\"tries\":" << opt.tries << ",";
            os << R"("service":")" << json_escape(opt.service) << "\",";
            os << R"("socktype":")" << json_escape(socktype_str(opt.socktype))
                    << "\",";
            os << R"("protocol":")" << json_escape(proto_str(opt.protocol)) <<
                    "\",";
            os << "\"flags\":{"
                    << "\"addrconfig\":" << (opt.addrconfig ? "true" : "false")
                    << ","
                    << "\"canonname\":" << (opt.canonname ? "true" : "false") <<
                    ","
                    << "\"all\":" << (opt.all ? "true" : "false") << ","
                    << "\"v4mapped\":" << (opt.v4mapped ? "true" : "false") <<
                    ","
                    << "\"numeric_host\":" << (opt.numeric_host
                                                   ? "true"
                                                   : "false")
                    << "},";
            os << "\"reverse\":" << (opt.reverse ? "true" : "false") << ",";
            os << "\"ni_namereqd\":" << (opt.ni_namereqd ? "true" : "false") <<
                    ",";
            os << "\"concurrency\":" << opt.concurrency << ",";
            os << "\"dedup\":" << (opt.dedup ? "true" : "false") << ",";
            os << R"("summary":{"min_ms":)" << minv << ",\"avg_ms\":" << avg <<
                    ",\"max_ms\":" << maxv << ",\"count\":" << times.size() <<
                    "},";
            if (!opt.pctl.empty())
            {
                os << "\"percentiles\":{";
                for (size_t i = 0; i < opt.pctl.size(); ++i)
                {
                    if (i) os << ",";
                    int p = opt.pctl[i];
                    os << "\"p" << p << "\":" << pct_value(p);
                }
                os << "},";
            }
            os << "\"attempts\":[";
            for (int i = 0; i < opt.tries; ++i)
            {
                const auto &ar = attempts[i];
                if (i) os << ",";
                os << "{";
                os << "\"try\":" << (i + 1) << ",\"ms\":" << ar.ms << ",\"rc\":"
                        << ar.rc;
                if (!ar.error.empty())
                    os << R"(,"error":")" << json_escape(
                        ar.error) << "\"";
                if (!ar.canon.empty())
                    os << R"(,"canon":")" << json_escape(
                        ar.canon) << "\"";
                os << ",\"addresses\":[";
                for (size_t j = 0; j < ar.entries.size(); ++j)
                {
                    const auto &e = ar.entries[j];
                    if (j) os << ",";
                    os << R"({"family":")" << json_escape(family_str(e.af)) <<
                            R"(","ip":")" << json_escape(e.ip)
                            << R"(","socktype":")" <<
                            json_escape(socktype_str(e.socktype)) <<
                            R"(","protocol":")" << json_escape(
                                proto_str(e.protocol))
                            << R"(","port":)" << e.port << "}";
                }
                os << "]";
                if (!ar.ptrs.empty())
                {
                    os << ",\"ptr\":[";
                    for (size_t k = 0; k < ar.ptrs.size(); ++k)
                    {
                        const auto &p = ar.ptrs[k];
                        if (k) os << ",";
                        os << R"({"family":")" << json_escape(family_str(p.af))
                                << R"(","ip":")" << json_escape(p.ip)
                                << R"(","rc":)" << p.rc;
                        if (p.rc == 0)
                            os << R"(,"name":")" << json_escape(
                                p.name) << "\"";
                        else
                            os << R"(,"error":")" << json_escape(p.error) <<
                                    "\"";
                        os << "}";
                    }
                    os << "]";
                }
                os << "}";
            }
            os << "]";
            os << "}";
            std::print("{}\n", os.str());
        }
        else if (!opt.ndjson)
        {
            std::println(
                "summary: min={:.3f} ms, avg={:.3f} ms, max={:.3f} ms ({} tries)",
                minv,
                avg,
                maxv,
                times.size());
            if (!opt.pctl.empty())
            {
                std::ostringstream os;
                os << "percentiles:";
                for (size_t i = 0; i < opt.pctl.size(); ++i)
                {
                    int p = opt.pctl[i];
                    if (i) os << ' ';
                    os << ' ' << 'p' << p << '=' << std::fixed <<
                            std::setprecision(3) << pct_value(p);
                    if (i + 1 < opt.pctl.size()) os << ',';
                }
                std::println("{}", os.str());
            }
        }
    }

    return 0;
}
