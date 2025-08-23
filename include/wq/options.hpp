#pragma once

#include <string>
#include <vector>

namespace wq
{
enum class Family { Any, IPv4, IPv6 };

struct Options
{
    std::string host;
    int tries = 3;
    Family family = Family::Any;
    // detailed controls
    bool addrconfig = true;    // AI_ADDRCONFIG
    bool canonname = true;     // AI_CANONNAME
    bool all = false;          // AI_ALL
    bool v4mapped = false;     // AI_V4MAPPED
    bool numeric_host = false; // AI_NUMERICHOST
    int socktype = 0;          // 0=any, SOCK_STREAM/DGRAM/RAW
    int protocol = 0;          // 0=any, IPPROTO_TCP/UDP
    std::string service;       // service name or port (optional)
    bool reverse = false;      // perform PTR lookups
    bool ni_namereqd = false;  // NI_NAMEREQD for getnameinfo
    int concurrency = 1;       // number of parallel attempts
    bool json = false;         // JSON output mode
    bool dedup = false;        // fold duplicate results
    bool ndjson = false;       // NDJSON streaming per attempt
    std::vector<int> pctl;     // requested percentiles (0..100)
    bool stop_on_error = false; // short-circuit remaining tries on error
    // Raw DNS (ldns) controls - Phase1
    std::string qtype;     // when non-empty, enable raw DNS path
    std::string ns;        // server IP/host (authoritative/recursive)
    bool rd = true;        // recursion desired bit
    bool do_bit = false;   // DNSSEC DO bit in EDNS
    int timeout_ms = 2000; // per-attempt timeout
    bool tcp = false;      // force TCP transport
};
} // namespace wq
