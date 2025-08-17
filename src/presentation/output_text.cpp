#include "wq/output.hpp"

#include <netdb.h>
#include <sstream>
#include <iomanip>

#include "wq/options.hpp"
#include "wq/model.hpp"

namespace wq {

const char *family_str(const int af)
{
    switch (af)
    {
        case AF_INET: return "inet";
        case AF_INET6: return "inet6";
        default: return "unspec";
    }
}

const char *socktype_str(const int st)
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

const char *proto_str(const int p)
{
    switch (p)
    {
        case 0: return "any";
        case IPPROTO_TCP: return "tcp";
        case IPPROTO_UDP: return "udp";
        default: return "other";
    }
}

static const char* family_to_str_from_enum(Family f)
{
    switch (f)
    {
        case Family::Any: return "any";
        case Family::IPv4: return "inet";
        case Family::IPv6: return "inet6";
    }
    return "any";
}

std::string format_header_text(const Options& opt)
{
    std::ostringstream os;
    os << "Resolving: " << opt.host << '\n';
    os << "Family: " << family_to_str_from_enum(opt.family)
       << "  Tries: " << opt.tries << '\n';
    os << "Flags: addrconfig=" << (opt.addrconfig ? "on" : "off")
       << " canonname=" << (opt.canonname ? "on" : "off")
       << " all=" << (opt.all ? "on" : "off")
       << " v4mapped=" << (opt.v4mapped ? "on" : "off")
       << " numeric-host=" << (opt.numeric_host ? "on" : "off") << '\n';
    os << "Socktype: " << socktype_str(opt.socktype)
       << "  Protocol: " << proto_str(opt.protocol)
       << "  Service: " << (opt.service.empty() ? "(none)" : opt.service.c_str())
       << '\n';
    os << "Reverse: " << (opt.reverse ? "on" : "off")
       << "  NI_NAMEREQD: " << (opt.ni_namereqd ? "on" : "off")
       << "  Concurrency: " << opt.concurrency
       << "  JSON: " << (opt.json ? "on" : "off")
       << "  Dedup: " << (opt.dedup ? "on" : "off") << '\n';
    if (!opt.qtype.empty())
    {
        os << "Raw DNS: type=" << opt.qtype
           << " ns=" << (opt.ns.empty() ? "(system)" : opt.ns.c_str())
           << " rd=" << (opt.rd ? "on" : "off")
           << " do=" << (opt.do_bit ? "on" : "off")
           << " timeout_ms=" << opt.timeout_ms
           << " tcp=" << (opt.tcp ? "on" : "off") << '\n';
    }
    return os.str();
}

std::string format_entries_text(const std::vector<Entry>& entries)
{
    std::ostringstream os;
    for (const auto &e: entries)
    {
        if (e.port)
        {
            os << "  - [" << family_str(e.af) << "] " << e.ip
               << "  socktype=" << socktype_str(e.socktype)
               << "  proto=" << proto_str(e.protocol)
               << "  port=" << e.port << '\n';
        }
        else
        {
            os << "  - [" << family_str(e.af) << "] " << e.ip
               << "  socktype=" << socktype_str(e.socktype)
               << "  proto=" << proto_str(e.protocol) << '\n';
        }
    }
    return os.str();
}

std::string format_ptrs_text(const std::vector<PtrItem>& ptrs)
{
    std::ostringstream os;
    for (const auto &p: ptrs)
    {
        if (p.rc == 0)
        {
            os << "  PTR: [" << family_str(p.af) << "] " << p.ip
               << " -> " << p.name << '\n';
        }
        else
        {
            os << "  PTR: [" << family_str(p.af) << "] " << p.ip
               << " -> <" << p.error << ">\n";
        }
    }
    return os.str();
}

std::string format_try_footer_text(int t, double ms, size_t address_count, const std::string& canon)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "try " << t << ": " << ms << " ms - " << address_count << " address(es)\n";
    if (!canon.empty()) os << "  canon: " << canon << '\n';
    return os.str();
}

std::string format_summary_text(double min_ms, double avg_ms, double max_ms, size_t tries)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "summary: min=" << min_ms
       << " ms, avg=" << avg_ms
       << " ms, max=" << max_ms
       << " ms (" << tries << " tries)\n";
    return os.str();
}

std::string format_percentiles_text(const std::vector<std::pair<int,double>>& pctl_values)
{
    if (pctl_values.empty()) return {};
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "percentiles: ";
    for (size_t i = 0; i < pctl_values.size(); ++i)
    {
        if (i) os << ", ";
        os << 'p' << pctl_values[i].first << '=' << pctl_values[i].second;
    }
    os << '\n';
    return os.str();
}

} // namespace wq
