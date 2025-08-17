#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace wq {

struct Entry {
    int         af{};
    int         socktype{};
    int         protocol{};
    uint16_t    port{};
    std::string ip;
};

struct PtrItem {
    int         af{};
    std::string ip;
    int         rc{};       // 0 if ok
    std::string name;       // valid if rc==0
    std::string error;      // valid if rc!=0
};

struct AttemptResult {
    double               ms{};
    int                  rc{};      // getaddrinfo rc
    std::string          error;     // if rc!=0
    std::string          canon;
    std::vector<Entry>   entries;
    std::vector<PtrItem> ptrs;      // may be empty when reverse disabled
};

} // namespace wq
