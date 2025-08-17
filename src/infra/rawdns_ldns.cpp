#include "wq/rawdns.hpp"

#include <chrono>
#include <string>
#include <vector>
#include <sys/time.h>

#ifdef HAVE_LDNS
#include <ldns/ldns.h>
#endif

namespace wq
{
RawDnsResult resolve_rawdns_once(const Options &opt)
{
    RawDnsResult out{};
    auto t0 = std::chrono::steady_clock::now();

#ifndef HAVE_LDNS
    auto t1 = std::chrono::steady_clock::now();
    out.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    out.rc = -1;
    out.kind = RawDnsErrorKind::NotAvailable;
    out.error =
            "ldns not available: rebuild with ldns (pkg-config ldns) to enable raw DNS";
    return out;
#else
    ldns_resolver *res = nullptr;
    ldns_status st = LDNS_STATUS_OK;

    if (opt.ns.empty())
    {
        st = ldns_resolver_new_frm_file(&res, nullptr);
    }
    else
    {
        res = ldns_resolver_new();
        if (res)
        {
            ldns_rdf *ns_rdf = nullptr;
            if (opt.ns.find(':') != std::string::npos)
            {
                ns_rdf = ldns_rdf_new_frm_str(
                    LDNS_RDF_TYPE_AAAA,
                    opt.ns.c_str());
            }
            else
            {
                ns_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, opt.ns.c_str());
            }
            if (ns_rdf)
            {
                (void) ldns_resolver_push_nameserver(res, ns_rdf);
                ldns_rdf_deep_free(ns_rdf);
            }
            else
            {
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
        auto t1 = std::chrono::steady_clock::now();
        out.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        out.rc = -1;
        out.kind = RawDnsErrorKind::InitFailed;
        out.error = "ldns_resolver init failed";
        if (res) ldns_resolver_deep_free(res);
        return out;
    }

    // Apply resolver settings
    ldns_resolver_set_recursive(res, opt.rd);
    ldns_resolver_set_usevc(res, opt.tcp);
    ldns_resolver_set_fallback(res, true);
    if (opt.timeout_ms >= 0)
    {
        struct timeval tv{
            .tv_sec = opt.timeout_ms / 1000,
            .tv_usec = (opt.timeout_ms % 1000) * 1000
        };
        ldns_resolver_set_timeout(res, tv);
    }
    ldns_resolver_set_edns_udp_size(res, 1232);
    ldns_resolver_set_dnssec(res, opt.do_bit);

    // Build qname and type
    ldns_rdf *name = ldns_dname_new_frm_str(opt.host.c_str());
    if (!name)
    {
        auto t1 = std::chrono::steady_clock::now();
        out.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        out.rc = -1;
        out.kind = RawDnsErrorKind::InvalidQname;
        out.error = "invalid qname";
        ldns_resolver_deep_free(res);
        return out;
    }

    ldns_rr_type qtype = ldns_get_rr_type_by_name(opt.qtype.c_str());
    if (qtype == 0)
    {
        static const std::pair<const char *, ldns_rr_type> kTypeMap[] = {
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

    ldns_pkt *pkt = nullptr;
    uint16_t qflags = 0;
    if (opt.rd) qflags |= LDNS_RD;
    st = ldns_resolver_query_status(
        &pkt,
        res,
        name,
        qtype,
        LDNS_RR_CLASS_IN,
        qflags);

    auto t1 = std::chrono::steady_clock::now();
    out.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (st != LDNS_STATUS_OK || !pkt)
    {
        out.rc = -1;
        out.kind = RawDnsErrorKind::QueryFailed;
        out.error = "ldns query failed";
        if (pkt) ldns_pkt_free(pkt);
        ldns_rdf_deep_free(name);
        ldns_resolver_deep_free(res);
        return out;
    }

    // Success: extract details
    out.rcode = static_cast<int>(ldns_pkt_get_rcode(pkt));
    out.f_aa = ldns_pkt_aa(pkt);
    out.f_tc = ldns_pkt_tc(pkt);
    out.f_rd = ldns_pkt_rd(pkt);
    out.f_ra = ldns_pkt_ra(pkt);
    out.f_ad = ldns_pkt_ad(pkt);
    out.f_cd = ldns_pkt_cd(pkt);

    ldns_rr_list *ans = ldns_pkt_answer(pkt);
    ldns_rr_list *auth = ldns_pkt_authority(pkt);
    ldns_rr_list *addl = ldns_pkt_additional(pkt);
    out.answer_count = ans ? ldns_rr_list_rr_count(ans) : 0;
    out.authority_count = auth ? ldns_rr_list_rr_count(auth) : 0;
    out.additional_count = addl ? ldns_rr_list_rr_count(addl) : 0;

    out.answers.clear();
    out.answers.reserve(out.answer_count);
    for (size_t i = 0; i < out.answer_count; ++i)
    {
        ldns_rr *rr = ldns_rr_list_rr(ans, i);
        if (char *s = ldns_rr2str(rr))
        {
            out.answers.emplace_back(s);
            LDNS_FREE(s);
        }
        else
        {
            out.answers.emplace_back("");
        }
    }

    out.rc = 0;

    if (pkt) ldns_pkt_free(pkt);
    ldns_rdf_deep_free(name);
    ldns_resolver_deep_free(res);
    return out;
#endif
}
} // namespace wq
