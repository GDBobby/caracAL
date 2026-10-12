#include "html_globals.hpp"

#include "string_util.hpp"

#include <cctype>
#include <regex>
#include <stdexcept>
#include <vector>

namespace caracal {
    namespace {

        bool contains_page_globals_marker(std::string_view s) {
            static const std::regex re(R"(var\s+inside\s*=\s*["']login["'])");
            return std::regex_search(s.begin(), s.end(), re);
        }

    } // namespace

    std::string extract_page_globals(std::string_view html) {
        std::string lower = to_lower(std::string(html));
        std::string block;
        std::size_t pos = 0;
        while (true) {
            auto open = lower.find("<script", pos);
            if (open == std::string::npos) {
                break;
            }
            auto gt = lower.find('>', open);
            if (gt == std::string::npos) {
                break;
            }
            auto open_tag = lower.substr(open, gt - open);
            auto src_attr = open_tag.find("src");
            if (src_attr != std::string::npos && open_tag.find('=', src_attr) != std::string::npos) {
                pos = gt + 1;
                continue;
            }
            auto close = lower.find("</script", gt);
            if (close == std::string::npos) {
                break;
            }
            auto inner = html.substr(gt + 1, close - (gt + 1));
            if (contains_page_globals_marker(inner)) {
                block = std::string(inner);
                break;
            }
            pos = close + 1;
        }
        if (block.empty()) {
            throw std::runtime_error("could not find official page globals script block");
        }

        std::vector<std::string> lines;
        std::string cur;
        for (char c : block) {
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            }
            else {
                cur.push_back(c);
            }
        }
        lines.push_back(cur);

        std::vector<std::string> out;
        for (const auto& line : lines) {
            auto trimmed = line;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            if (trimmed.starts_with("if") && trimmed.size() > 2 &&
                (trimmed[2] == ' ' || trimmed[2] == '\t' || trimmed[2] == '(')) {
                std::size_t i = 2;
                while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i]))) {
                    ++i;
                }
                if (i < trimmed.size() && trimmed[i] == '(') {
                    break;
                }
            }
            if (trimmed.empty() || trimmed.starts_with("//") || trimmed.starts_with("var ") ||
                trimmed.starts_with("function ")) {
                out.push_back(line);
                continue;
            }
            if (!out.empty()) {
                break;
            }
        }
        if (out.empty()) {
            throw std::runtime_error("page globals script block was empty after extraction");
        }
        std::string result;
        for (std::size_t i = 0; i < out.size(); ++i) {
            result += out[i];
            result += '\n';
        }
        return result;
    }

} // namespace caracal
