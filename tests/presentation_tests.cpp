#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <netdb.h>

#include "wq/output.hpp"
#include "wq/options.hpp"
#include "wq/model.hpp"

using namespace wq;

static void assert_true(bool cond, std::string_view msg)
{
    if (!cond)
    {
        std::cerr << "ASSERT FAILED: " << msg << std::endl;
        std::exit(1);
    }
}

static void assert_contains(const std::string& haystack, std::string_view needle, std::string_view msg)
{
    if (haystack.find(needle) == std::string::npos)
    {
        std::cerr << "ASSERT FAILED: missing substring: " << needle << " | " << msg << std::endl;
        std::cerr << "Actual: " << haystack << std::endl;
        std::exit(1);
    }
}

static void test_format_header_text_basic()
{
    Options opt{};
    opt.host = "example.com";
    opt.tries = 3;
    opt.family = Family::IPv4;
    opt.addrconfig = true;
    opt.canonname = true;
    opt.all = false;
    opt.v4mapped = false;
    opt.numeric_host = false;
    opt.socktype = SOCK_STREAM;
    opt.protocol = IPPROTO_TCP;
    opt.service = "80";
    opt.reverse = true;
    opt.ni_namereqd = false;
    opt.concurrency = 2;
    opt.json = false;
    opt.dedup = false;
    
    std::string s = format_header_text(opt);
    assert_contains(s, "Resolving: example.com\n", "header: title");
    assert_contains(s, "Family: inet  Tries: 3\n", "header: family/tries");
    assert_contains(s, "Flags: addrconfig=on canonname=on all=off v4mapped=off numeric-host=off\n", "header: flags");
    assert_contains(s, "Socktype: stream  Protocol: tcp  Service: 80\n", "header: sock/proto/service");
    assert_contains(s, "Reverse: on  NI_NAMEREQD: off  Concurrency: 2  JSON: off  Dedup: off\n", "header: reverse/etc");
}

static void test_format_header_text_rawdns()
{
    Options opt{};
    opt.host = "example.com";
    opt.tries = 1;
    opt.family = Family::Any;
    opt.socktype = 0;
    opt.protocol = 0;
    opt.qtype = "A";
    opt.ns = "1.1.1.1";
    opt.rd = true;
    opt.do_bit = true;
    opt.timeout_ms = 1234;
    opt.tcp = true;

    std::string s = format_header_text(opt);
    assert_contains(s, "Raw DNS: type=A ns=1.1.1.1 rd=on do=on timeout_ms=1234 tcp=on\n", "rawdns line");
}

static void test_format_summary_and_percentiles()
{
    std::string summary = format_summary_text(1.234, 2.345, 3.456, 5);
    assert_true(summary == std::string("summary: min=1.234 ms, avg=2.345 ms, max=3.456 ms (5 tries)\n"), "summary exact");

    std::vector<std::pair<int,double>> pct{{50, 10.5}, {90, 20.75}};
    std::string ptxt = format_percentiles_text(pct);
    assert_true(ptxt == std::string("percentiles: p50=10.500, p90=20.750\n"), "percentiles exact");
}

static void test_build_final_json_minimal()
{
    Options opt{};
    opt.host = "example.com";
    opt.tries = 1;
    opt.family = Family::IPv4;
    opt.service = "80";
    opt.socktype = SOCK_STREAM;
    opt.protocol = IPPROTO_TCP;

    AttemptResult ar{};
    ar.ms = 12.345;
    ar.rc = 0;
    ar.canon = "example.com";
    Entry e{}; e.af = AF_INET; e.socktype = SOCK_STREAM; e.protocol = IPPROTO_TCP; e.port = 80; e.ip = "93.184.216.34";
    ar.entries.push_back(e);

    std::vector<AttemptResult> attempts{ar};
    std::vector<std::pair<int,double>> pct; // empty

    std::string js = build_final_json(opt, 12.345, 12.345, 12.345, pct, attempts);
    assert_contains(js, "\"host\":\"example.com\"", "host field");
    assert_contains(js, "\"tries\":1", "tries field");
    assert_contains(js, "\"family\":\"inet\"", "family field");
    assert_contains(js, "\"summary\":{\"min_ms\":12.345,\"avg_ms\":12.345,\"max_ms\":12.345,\"count\":1}", "summary block");
    assert_contains(js, "\"attempts\":[{\"try\":1,\"ms\":12.345,\"rc\":0", "attempt header");
    assert_contains(js, "\"addresses\":[{\"family\":\"inet\",\"ip\":\"93.184.216.34\",\"socktype\":\"stream\",\"protocol\":\"tcp\",\"port\":80}]", "address item");
}

int main()
{
    test_format_header_text_basic();
    test_format_header_text_rawdns();
    test_format_summary_and_percentiles();
    test_build_final_json_minimal();
    std::cout << "presentation tests: OK" << std::endl;
    return 0;
}
