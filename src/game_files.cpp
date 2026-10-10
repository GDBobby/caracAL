#include "game_files.hpp"

#include "client_scripts.hpp"
#include "html_globals.hpp"
#include "html_prelude.hpp"
#include "http_client.hpp"
#include "log_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace caracal::game_files {
namespace fs = std::filesystem;

namespace {

bool file_exists(const fs::path& p) { return fs::exists(p); }

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

void write_file(const fs::path& p, std::string_view data) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::optional<Json> read_manifest(std::string_view base_url, std::string_view version) {
  auto path = locate_client_scripts_manifest(base_url, version);
  if (!file_exists(path)) return std::nullopt;
  try {
    auto raw = Json::parse(read_file(path));
    if (!raw.is_object() || !raw["game"].is_array() || !raw["runner"].is_array() ||
        raw["game"].as_array().empty() || raw["runner"].as_array().empty()) {
      return std::nullopt;
    }
    return raw;
  } catch (...) {
    log().warn("invalid client_scripts.json for version " + std::string(version));
    return std::nullopt;
  }
}

void write_manifest(std::string_view base_url, std::string_view version, const Json& game,
                    const Json& runner) {
  Json m = Json::object();
  try {
    m["version"] = std::stoi(std::string(version));
  } catch (...) {
    m["version"] = std::string(version);
  }
  m["generated_from"] = "official index + runner HTML";
  m["game"] = game;
  m["runner"] = runner;
  auto path = locate_client_scripts_manifest(base_url, version);
  write_file(path, m.dump(2) + "\n");
}

std::string fetch_index_html(std::string_view base_url) {
  auto r = http_get(std::string(base_url));
  if (r.status < 200 || r.status >= 300) {
    throw std::runtime_error("failed to fetch index html: " + r.status_text);
  }
  return r.body;
}

std::string fetch_runner_html(std::string_view base_url) {
  std::string url(base_url);
  if (!url.ends_with('/')) url += '/';
  url += "runner";
  auto r = http_get(url);
  if (r.status < 200 || r.status >= 300) {
    throw std::runtime_error("failed to fetch runner html: " + r.status_text);
  }
  return r.body;
}

void download_file(const std::string& url, const fs::path& dest) {
  auto r = http_get(url);
  if (r.status < 200 || r.status >= 300) {
    throw std::runtime_error("failed to download " + url + ": " + r.status_text);
  }
  write_file(dest, r.body);
}

}  // namespace

bool follows_latest_client_any(const Json& version) {
  if (version.is_null()) return true;
  if (version.is_number()) return version.as_number() == 0;
  if (version.is_string()) {
    auto s = version.as_string();
    return s.empty() || s == "0";
  }
  return false;
}

bool follows_latest_client(const std::optional<Json>& version) {
  if (!version) return true;
  return follows_latest_client_any(*version);
}

std::vector<std::string> get_runner_files_fallback() {
  return {
      "/js/old_common_functions.js",
      "/js/common_functions.js",
      "/js/runner_functions.js",
      "/js/runner_compat.js",
  };
}

std::vector<std::string> get_game_files_fallback() {
  return {
      "/js/pixi/fake/pixi.min.js",
      "/js/libraries/combined.js",
      "/js/codemirror/fake/codemirror.js",
      "/js/old_common_functions.js",
      "/js/common_functions.js",
      "/js/functions.js",
      "/js/game.js",
      "/js/html.js",
      "/js/merrit_stand_notice.js",
      "/js/payments.js",
      "/js/keyboard.js",
      "/data.js",
  };
}

std::string hostname(std::string_view base_url) { return parse_url(base_url).host; }

std::string locate_game_file(std::string_view base_url, std::string_view resource,
                             std::string_view version) {
  fs::path p = resource;
  auto name = p.filename().string();
  return "./game_files/" + hostname(base_url) + "/" + std::string(version) + "/" + name;
}

std::string locate_html_globals(std::string_view base_url, std::string_view version) {
  return locate_game_file(base_url, "/html_globals.js", version);
}

std::string locate_client_scripts_manifest(std::string_view base_url,
                                           std::string_view version) {
  return "./game_files/" + hostname(base_url) + "/" + std::string(version) +
         "/client_scripts.json";
}

std::vector<std::string> get_game_files(std::string_view base_url, std::string_view version) {
  if (!base_url.empty() && !version.empty()) {
    if (auto m = read_manifest(base_url, version)) {
      std::vector<std::string> out;
      for (const auto& x : (*m)["game"].as_array()) out.push_back(x.as_string());
      return out;
    }
  }
  return get_game_files_fallback();
}

std::vector<std::string> get_runner_files(std::string_view base_url, std::string_view version) {
  if (!base_url.empty() && !version.empty()) {
    if (auto m = read_manifest(base_url, version)) {
      std::vector<std::string> out;
      for (const auto& x : (*m)["runner"].as_array()) out.push_back(x.as_string());
      return out;
    }
  }
  return get_runner_files_fallback();
}

std::vector<std::string> get_all_client_files(std::string_view base_url,
                                              std::string_view version) {
  auto files = get_game_files(base_url, version);
  for (auto& f : get_runner_files(base_url, version)) {
    if (std::find(files.begin(), files.end(), f) == files.end()) files.push_back(f);
  }
  return files;
}

std::string official_download_url(std::string_view base_url, std::string_view resource) {
  std::string b(base_url);
  while (b.ends_with('/')) b.pop_back();
  return b + std::string(resource);
}

std::string resolve_html_globals_source(std::string_view base_url, std::string_view version) {
  auto cached = locate_html_globals(base_url, version);
  if (file_exists(cached)) return cached;
  log().warn("html_globals.js missing for version " + std::string(version) +
             "; falling back to embedded html_prelude");
  return {};
}

std::string write_html_globals(std::string_view base_url, std::string_view version,
                               std::string_view html) {
  auto path = locate_html_globals(base_url, version);
  std::string script =
      "// Auto-extracted from official page HTML — do not edit.\n" +
      extract_page_globals(html);
  write_file(path, script);
  return path;
}

std::string ensure_html_globals(std::string_view base_url, std::string_view version,
                                std::optional<std::string> html) {
  auto path = locate_html_globals(base_url, version);
  if (file_exists(path)) return path;
  auto page = html ? *html : fetch_index_html(base_url);
  return write_html_globals(base_url, version, page);
}

std::vector<int> available_versions(std::string_view base_url) {
  std::vector<int> out;
  fs::path dir = "./game_files/" + hostname(base_url);
  if (!fs::exists(dir)) return out;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (!e.is_directory()) continue;
    auto name = e.path().filename().string();
    if (std::regex_match(name, std::regex(R"(^\d+$)"))) out.push_back(std::stoi(name));
  }
  std::sort(out.begin(), out.end(), std::greater<>());
  return out;
}

void cull_versions(std::string_view base_url, const std::vector<int>& exclusions) {
  auto all = available_versions(base_url);
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (i < 2) continue;
    if (std::find(exclusions.begin(), exclusions.end(), all[i]) != exclusions.end()) continue;
    try {
      log().info("culling version " + std::to_string(all[i]));
      fs::remove_all("./game_files/" + hostname(base_url) + "/" + std::to_string(all[i]));
    } catch (const std::exception& e) {
      log().warn(std::string("failed to cull version ") + std::to_string(all[i]) + " " +
                 e.what());
    }
  }
}

int ensure_latest(std::string_view base_url) {
  auto html = fetch_index_html(base_url);
  std::smatch match;
  static const std::regex ver_re(R"(game\.js\?v=([0-9]+)\")");
  if (!std::regex_search(html, match, ver_re)) {
    throw std::runtime_error("malformed version response");
  }
  int version = std::stoi(match[1]);
  auto runner_html = fetch_runner_html(base_url);
  auto game = client_scripts::resolve_game_files_from_index_html(html);
  auto runner = client_scripts::resolve_runner_files_from_runner_html(runner_html);
  Json g = Json::array();
  Json r = Json::array();
  for (auto& x : game) g.as_array().push_back(x);
  for (auto& x : runner) r.as_array().push_back(x);
  std::string ver = std::to_string(version);
  fs::create_directories("./game_files/" + hostname(base_url) + "/" + ver);
  write_manifest(base_url, ver, g, r);
  write_html_globals(base_url, ver, html);
  auto targets = get_all_client_files(base_url, ver);
  std::vector<std::string> missing;
  for (auto& resource : targets) {
    auto dest = locate_game_file(base_url, resource, ver);
    if (!file_exists(dest)) missing.push_back(resource);
  }
  if (missing.empty()) {
    log().info("all files for version " + ver + " are present");
  } else {
    log().info("downloading game files for version " + ver);
    for (auto& itm : missing) {
      download_file(official_download_url(base_url, itm),
                    locate_game_file(base_url, itm, ver));
    }
  }
  return version;
}

}  // namespace caracal::game_files
