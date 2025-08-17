#pragma once

#include <string>
#include <vector>

#include "wq/options.hpp"

namespace wq {

enum class RawDnsErrorKind {
    None = 0,
    NotAvailable,
    InitFailed,
    InvalidQname,
    QueryFailed,
};

struct RawDnsResult {
    double ms{};
    int rc{};                 // 0 on success, -1 on error
    std::string error;        // error message when rc != 0
    RawDnsErrorKind kind{RawDnsErrorKind::None};

    // Success fields
    int  rcode{};
    bool f_aa{};
    bool f_tc{};
    bool f_rd{};
    bool f_ra{};
    bool f_ad{};
    bool f_cd{};
    size_t answer_count{};
    size_t authority_count{};
    size_t additional_count{};
    std::vector<std::string> answers; // textual RR lines
};

// Perform one raw DNS query using ldns when available.
// When ldns is not available at build time, returns rc = -1 and kind = NotAvailable.
RawDnsResult resolve_rawdns_once(const Options& opt);

} // namespace wq
