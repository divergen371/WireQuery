#include "wq/resolver.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <vector>
#include <string>

// POSIX networking
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace wq
{
static int family_to_af(const Family f)
{
    switch (f)
    {
        case Family::IPv4: return AF_INET;
        case Family::IPv6: return AF_INET6;
        default: return AF_UNSPEC;
    }
}

static std::vector<Entry> collect_entries(const addrinfo *res, bool dedup)
{
    std::vector<Entry> out;
    std::unordered_set<std::string> seen;
    char buf[INET6_ADDRSTRLEN]{};
    for (const addrinfo *ai = res; ai != nullptr; ai = ai->ai_next)
    {
        Entry e{};
        e.af = ai->ai_family;
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
                sizeof(buf))) e.ip = buf;
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
                sizeof(buf))) e.ip = buf;
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
    bool namereqd)
{
    std::vector<PtrItem> out;
    std::unordered_set<std::string> seen; // key: af|ip
    char name[NI_MAXHOST]{};
    for (const auto &[af, socktype, protocol, port, ip]: entries)
    {
        if (std::string key = std::to_string(af) + '|' + ip; !seen.insert(key).
            second) continue;
        PtrItem item{};
        item.af = af;
        item.ip = ip;
        if (af == AF_INET)
        {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &sin.sin_addr);
            int flags = NI_NOFQDN | (namereqd ? NI_NAMEREQD : 0);
            if (int rc = getnameinfo(
                reinterpret_cast<sockaddr *>(&sin),
                sizeof(sin),
                name,
                sizeof(name),
                nullptr,
                0,
                flags); rc == 0)
            {
                item.rc = rc;
                item.name = std::string{name};
            }
            else
            {
                item.rc = rc;
                item.error = gai_strerror(rc);
            }
        }
        else if (af == AF_INET6)
        {
            sockaddr_in6 sin6{};
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port = htons(port);
            inet_pton(AF_INET6, ip.c_str(), &sin6.sin6_addr);
            int flags = NI_NOFQDN | (namereqd ? NI_NAMEREQD : 0);
            if (int rc = getnameinfo(
                reinterpret_cast<sockaddr *>(&sin6),
                sizeof(sin6),
                name,
                sizeof(name),
                nullptr,
                0,
                flags); rc == 0)
            {
                item.rc = rc;
                item.name = std::string{name};
            }
            else
            {
                item.rc = rc;
                item.error = gai_strerror(rc);
            }
        }
        out.push_back(std::move(item));
    }
    return out;
}

AttemptResult resolve_posix_once(const Options &opt)
{
    AttemptResult result{};

    addrinfo hints{};
    hints.ai_family = family_to_af(opt.family);
    hints.ai_socktype = opt.socktype; // 0 = any
    hints.ai_protocol = opt.protocol; // 0 = any
    hints.ai_flags = 0;
    if (opt.addrconfig) hints.ai_flags |= AI_ADDRCONFIG;
    if (opt.canonname) hints.ai_flags |= AI_CANONNAME;
    if (opt.all) hints.ai_flags |= AI_ALL;
    if (opt.v4mapped) hints.ai_flags |= AI_V4MAPPED;
    if (opt.numeric_host) hints.ai_flags |= AI_NUMERICHOST;

    addrinfo *res = nullptr;
    const char *service = opt.service.empty() ? nullptr : opt.service.c_str();
    auto t0 = std::chrono::steady_clock::now();
    int rc = getaddrinfo(opt.host.c_str(), service, &hints, &res);
    auto t1 = std::chrono::steady_clock::now();
    result.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.rc = rc;

    if (rc != 0)
    {
        result.error = gai_strerror(rc);
        if (res) freeaddrinfo(res);
        return result;
    }

    // Build entries (with optional dedup) and reverse
    result.entries = collect_entries(res, opt.dedup);
    if (opt.reverse) result.ptrs = do_reverse_for_entries(
                         result.entries,
                         opt.ni_namereqd);

    result.canon = (res && res->ai_canonname)
                       ? std::string(res->ai_canonname)
                       : std::string();

    if (res) freeaddrinfo(res);
    return result;
}
} // namespace wq
