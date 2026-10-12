#include "g_data.hpp"

#include <fstream>
#include <regex>
#include <sstream>

namespace caracal {

    Json try_parse_data_js(std::string_view src) {
        std::string s(src);
        auto start = s.find('{');
        auto end = s.rfind('}');
        if (start == std::string::npos || end == std::string::npos || end <= start) {
            throw std::runtime_error("data.js: no object");
        }
        s = s.substr(start, end - start + 1);
        s = std::regex_replace(s, std::regex(R"(\bundefined\b)"), "null");
        return Json::parse(s);
    }

    Json load_g_data(std::string_view path) {
        std::ifstream in{std::string(path)};
        if (!in) {
            throw std::runtime_error("cannot read " + std::string(path));
        }
        std::ostringstream os;
        os << in.rdbuf();
        auto raw = os.str();
        try {
            return Json::parse(raw);
        } catch (...) {}
        return try_parse_data_js(raw);
    }

} // namespace caracal
