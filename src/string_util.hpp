#pragma once

#include <cctype>
#include <string>

namespace caracal {

inline std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace caracal
