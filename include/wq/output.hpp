#pragma once

#include <string>
#include <vector>

namespace wq
{
const char *family_str(int af);

const char *socktype_str(int st);

const char *proto_str(int p);

// Forward declarations to avoid heavy includes in header
struct Options;
struct Entry;
struct PtrItem;
struct AttemptResult;

// Text formatting (returns complete text block with trailing newlines when applicable)
std::string format_header_text(const Options &opt);

std::string format_entries_text(const std::vector<Entry> &entries);

std::string format_ptrs_text(const std::vector<PtrItem> &ptrs);

std::string format_try_footer_text(int t,
                                   double ms,
                                   size_t address_count,
                                   const std::string &canon);

std::string format_summary_text(double min_ms,
                                double avg_ms,
                                double max_ms,
                                size_t tries);

std::string format_percentiles_text(
    const std::vector<std::pair<int, double> > &pctl_values);

// NDJSON builders (single-line JSON strings without trailing newline)
std::string build_ndjson_rawdns_init_failed(int t,
                                            double ms,
                                            const std::string &err,
                                            const Options &opt);

std::string build_ndjson_rawdns_error_with_type(
    int t,
    double ms,
    const std::string &err,
    const std::string &qtype);

std::string build_ndjson_ldns_not_available(int t,
                                            double ms,
                                            const std::string &err,
                                            const Options &opt);

std::string build_ndjson_getaddrinfo_error(int t, double ms, int rc);

std::string build_ndjson_normal(int t,
                                double ms,
                                const std::string &canon,
                                const std::vector<Entry> &entries,
                                const std::vector<PtrItem> &ptrs);

// ldns path success (raw DNS result)
std::string build_ndjson_rawdns_success(int t,
                                        double ms,
                                        const std::string &qtype,
                                        int rcode,
                                        bool aa,
                                        bool tc,
                                        bool rd,
                                        bool ra,
                                        bool ad,
                                        bool cd,
                                        size_t answer_count,
                                        size_t authority_count,
                                        size_t additional_count,
                                        const std::vector<std::string> &
                                        answers);

// Final JSON (single object string without trailing newline)
std::string build_final_json(const Options &opt,
                             double min_ms,
                             double avg_ms,
                             double max_ms,
                             const std::vector<std::pair<int, double> > &
                             pctl_values,
                             const std::vector<AttemptResult> &attempts);
} // namespace wq
