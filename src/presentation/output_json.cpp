#include "wq/output.hpp"

#include <netdb.h>
#include <sstream>
#include <iomanip>

#include "wq/options.hpp"
#include "wq/model.hpp"
#include "wq/json.hpp"

namespace wq
{
std::string build_ndjson_rawdns_init_failed(int t,
                                            double ms,
                                            const std::string &err,
                                            const Options &opt)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
    os << R"(,"error":")" << json_escape(err) << R"(")";
    os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype)
            << R"(","ns":")" << json_escape(opt.ns)
            << R"(","rd":)" << (opt.rd ? "true" : "false")
            << R"(,"do":)" << (opt.do_bit ? "true" : "false")
            << R"(,"timeout_ms":)" << opt.timeout_ms
            << R"(,"tcp":)" << (opt.tcp ? "true" : "false") << "}";
    os << "}";
    return os.str();
}

std::string build_ndjson_rawdns_error_with_type(
    int t,
    double ms,
    const std::string &err,
    const std::string &qtype)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
    os << R"(,"error":")" << json_escape(err) << R"(")";
    os << R"(,"raw_dns":{"type":")" << json_escape(qtype) << R"("}})";
    return os.str();
}

std::string build_ndjson_ldns_not_available(int t,
                                            double ms,
                                            const std::string &err,
                                            const Options &opt)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":-1";
    os << R"(,"error":")" << json_escape(err) << R"(")";
    os << R"(,"raw_dns":{"type":")" << json_escape(opt.qtype)
            << R"(","ns":")" << json_escape(opt.ns)
            << R"(","rd":)" << (opt.rd ? "true" : "false")
            << R"(,"do":)" << (opt.do_bit ? "true" : "false")
            << R"(,"timeout_ms":)" << opt.timeout_ms
            << R"(,"tcp":)" << (opt.tcp ? "true" : "false") << "}";
    os << "}";
    return os.str();
}

std::string build_ndjson_getaddrinfo_error(int t, double ms, int rc)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":" << rc;
    os << R"(,"error":")" << json_escape(gai_strerror(rc)) << R"(")";
    os << "}";
    return os.str();
}

std::string build_ndjson_normal(int t,
                                double ms,
                                const std::string &canon,
                                const std::vector<Entry> &entries,
                                const std::vector<PtrItem> &ptrs)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":0";
    if (!canon.empty()) os << R"(,"canon":")" << json_escape(canon) << R"(")";
    os << ",\"addresses\":[";
    for (size_t j = 0; j < entries.size(); ++j)
    {
        const auto &e = entries[j];
        if (j) os << ",";
        os << R"({"family":")" << json_escape(family_str(e.af))
                << R"(","ip":")" << json_escape(e.ip)
                << R"(","socktype":")" << json_escape(socktype_str(e.socktype))
                << R"(","protocol":")" << json_escape(proto_str(e.protocol))
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
            if (p.rc == 0) os << R"(,"name":")" << json_escape(p.name) <<
                           R"(")";
            else os << R"(,"error":")" << json_escape(p.error) << R"(")";
            os << "}";
        }
        os << "]";
    }
    os << "}";
    return os.str();
}

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
                                        const std::vector<std::string> &answers)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << "\"try\":" << t << ",\"ms\":" << ms << ",\"rc\":0";
    os << R"(,"raw_dns":{"type":")" << json_escape(qtype)
            << R"(","rcode":)" << rcode
            << R"(,"flags":{"aa":)" << (aa ? "true" : "false")
            << R"(,"tc":)" << (tc ? "true" : "false")
            << R"(,"rd":)" << (rd ? "true" : "false")
            << R"(,"ra":)" << (ra ? "true" : "false")
            << R"(,"ad":)" << (ad ? "true" : "false")
            << R"(,"cd":)" << (cd ? "true" : "false") << "}"
            << R"(,"counts":{"answer":)" << answer_count
            << R"(,"authority":)" << authority_count
            << R"(,"additional":)" << additional_count << "}";
    os << ",\"answers\":[";
    for (size_t i = 0; i < answers.size(); ++i)
    {
        if (i) os << ",";
        os << R"(")" << json_escape(answers[i]) << R"(")";
    }
    os << "]}"; // close raw_dns and outer object
    return os.str();
}

std::string build_final_json(const Options &opt,
                             double min_ms,
                             double avg_ms,
                             double max_ms,
                             const std::vector<std::pair<int, double> > &
                             pctl_values,
                             const std::vector<AttemptResult> &attempts)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{";
    os << R"("host":")" << json_escape(opt.host) << R"(",)";
    os << R"("family":")" << (opt.family == Family::Any
                                  ? "any"
                                  : (opt.family == Family::IPv4
                                         ? "inet"
                                         : "inet6")) << R"(",)";
    os << R"("tries":)" << opt.tries << ",";
    os << R"("service":")" << json_escape(opt.service) << R"(",)";
    os << R"("socktype":")" << json_escape(socktype_str(opt.socktype)) <<
            R"(",)";
    os << R"("protocol":")" << json_escape(proto_str(opt.protocol)) << R"(",)";
    os << R"("flags":{)"
            << R"("addrconfig":)" << (opt.addrconfig ? "true" : "false") << ","
            << R"("canonname":)" << (opt.canonname ? "true" : "false") << ","
            << R"("all":)" << (opt.all ? "true" : "false") << ","
            << R"("v4mapped":)" << (opt.v4mapped ? "true" : "false") << ","
            << R"("numeric_host":)" << (opt.numeric_host ? "true" : "false")
            << "},";
    os << R"("reverse":)" << (opt.reverse ? "true" : "false") << ",";
    os << R"("ni_namereqd":)" << (opt.ni_namereqd ? "true" : "false") << ",";
    os << R"("concurrency":)" << opt.concurrency << ",";
    os << R"("dedup":)" << (opt.dedup ? "true" : "false") << ",";
    os << R"("summary":{"min_ms":)" << min_ms << ",\"avg_ms\":" << avg_ms <<
            ",\"max_ms\":" << max_ms << ",\"count\":" << attempts.size() <<
            "},";
    if (!pctl_values.empty())
    {
        os << R"("percentiles":{)";
        for (size_t i = 0; i < pctl_values.size(); ++i)
        {
            if (i) os << ",";
            os << R"("p)" << pctl_values[i].first << R"(":)" << pctl_values[i].
                    second;
        }
        os << "},";
    }
    os << R"("attempts":[)";
    for (size_t i = 0; i < attempts.size(); ++i)
    {
        const auto &ar = attempts[i];
        if (i) os << ",";
        os << "{";
        os << R"("try":)" << (i + 1) << R"(,"ms":)" << ar.ms << R"(,"rc":)" <<
                ar.rc;
        if (!ar.error.empty()) os << R"(,"error":")" << json_escape(ar.error) <<
                               R"(")";
        if (!ar.canon.empty()) os << R"(,"canon":")" << json_escape(ar.canon) <<
                               R"(")";
        os << R"(,"addresses":[)";
        for (size_t j = 0; j < ar.entries.size(); ++j)
        {
            const auto &e = ar.entries[j];
            if (j) os << ",";
            os << R"({"family":")" << json_escape(family_str(e.af))
                    << R"(","ip":")" << json_escape(e.ip)
                    << R"(","socktype":")" << json_escape(
                        socktype_str(e.socktype))
                    << R"(","protocol":")" << json_escape(proto_str(e.protocol))
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
                if (p.rc == 0) os << R"(,"name":")" << json_escape(p.name) <<
                               R"(")";
                else os << R"(,"error":")" << json_escape(p.error) << R"(")";
                os << "}";
            }
            os << "]";
        }
        os << "}";
    }
    os << "]";
    os << "}";
    return os.str();
}
} // namespace wq
