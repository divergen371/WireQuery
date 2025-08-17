#include "wq/json.hpp"

#include <cstdio>

namespace wq {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char uc : s) {
        switch (char c = static_cast<char>(uc)) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace wq
