#include "client_scripts.hpp"

#include <cctype>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace caracal::client_scripts {
namespace {

std::string to_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool starts_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

bool iequals_prefix(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<std::string> rewrite_or_skip(std::string src) {
  static const std::vector<std::pair<std::string, std::string>> rewrite = {
      {"/js/pixi/", "/js/pixi/fake/pixi.min.js"},
      {"/js/codemirror/", "/js/codemirror/fake/codemirror.js"},
  };
  static const std::unordered_set<std::string> keep_exact = {
      "/data.js",
      "/js/libraries/combined.js",
  };
  static const std::vector<std::string> skip_prefix = {
      "/js/pixi/",
      "/js/pixi-layers/",
      "/js/pixi-filters/",
      "/js/socket.io/",
      "/js/jquery/",
      "/js/howler/",
      "/js/libraries/",
      "/js/codemirror/",
      "/js/ios-drag-drop",
  };

  for (const auto& [prefix, fake] : rewrite) {
    if (src == prefix || starts_with(src, prefix)) return fake;
  }
  if (keep_exact.contains(src)) return src;
  for (const auto& skip : skip_prefix) {
    if (src == skip || starts_with(src, skip)) return std::nullopt;
  }
  if (starts_with(src, "/js/") && src.ends_with(".js")) {
    auto rest = src.substr(4);
    if (rest.find('/') == std::string::npos) return src;
  }
  return std::nullopt;
}

std::vector<std::string> unique(const std::vector<std::string>& items) {
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  for (const auto& item : items) {
    if (seen.insert(item).second) out.push_back(item);
  }
  return out;
}

}  // namespace

std::vector<std::string> extract_script_srcs(std::string_view html) {
  std::vector<std::string> out;
  std::string lower = to_lower(std::string(html));
  std::size_t pos = 0;
  while (true) {
    auto tag = lower.find("<script", pos);
    if (tag == std::string::npos) break;
    auto end = lower.find('>', tag);
    if (end == std::string::npos) break;
    std::string_view tagv = html.substr(tag, end - tag);
    std::string tag_lower = to_lower(std::string(tagv));
    auto src_at = tag_lower.find("src");
    pos = end + 1;
    if (src_at == std::string::npos) continue;
    std::size_t eq = tag_lower.find('=', src_at);
    if (eq == std::string::npos) continue;
    std::size_t i = eq + 1;
    while (i < tag_lower.size() && std::isspace(static_cast<unsigned char>(tag_lower[i]))) ++i;
    if (i >= tag_lower.size()) continue;
    char q = tagv[i];
    if (q != '"' && q != '\'') continue;
    ++i;
    auto qend = std::string(tagv).find(q, i);
    if (qend == std::string::npos) continue;
    std::string src(tagv.substr(i, qend - i));
    while (!src.empty() && std::isspace(static_cast<unsigned char>(src.front()))) src.erase(src.begin());
    while (!src.empty() && std::isspace(static_cast<unsigned char>(src.back()))) src.pop_back();
    if (src.empty() || starts_with(src, "http://") || starts_with(src, "https://")) continue;
    auto qpos = src.find('?');
    if (qpos != std::string::npos) src.resize(qpos);
    if (!src.starts_with('/')) src.insert(src.begin(), '/');
    out.push_back(std::move(src));
  }
  (void)iequals_prefix;
  return out;
}

std::vector<std::string> resolve_game_files_from_index_html(std::string_view index_html) {
  std::vector<std::string> mapped;
  for (auto& src : extract_script_srcs(index_html)) {
    if (auto r = rewrite_or_skip(std::move(src))) mapped.push_back(*r);
  }
  std::vector<std::string> required_head = {
      "/js/pixi/fake/pixi.min.js",
      "/js/libraries/combined.js",
      "/js/codemirror/fake/codemirror.js",
  };
  required_head.insert(required_head.end(), mapped.begin(), mapped.end());
  return unique(required_head);
}

std::vector<std::string> resolve_runner_files_from_runner_html(std::string_view runner_html) {
  std::vector<std::string> mapped;
  for (auto& src : extract_script_srcs(runner_html)) {
    if (auto r = rewrite_or_skip(std::move(src))) mapped.push_back(*r);
  }
  return unique(mapped);
}

}  // namespace caracal::client_scripts
