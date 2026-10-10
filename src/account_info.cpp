#include "account_info.hpp"

#include "http_client.hpp"
#include "log_utils.hpp"

#include <stdexcept>

namespace caracal {

AccountInfo::AccountInfo(std::string base_url, std::string session)
    : base_url_(std::move(base_url)), session_(std::move(session)) {}

Json AccountInfo::update_info() {
  log().info("updating account info");
  std::string url = base_url_;
  while (url.ends_with('/')) url.pop_back();
  url += "/api/servers_and_characters";
  auto r = http_post_json(url, "{}", {{"Cookie", "auth=" + session_}});
  if (r.status < 200 || r.status >= 300) {
    throw std::runtime_error("failed to update account info: " + r.status_text);
  }
  auto data = Json::parse(r.body);
  Json res;
  if (data.is_object() && data["infs"].is_array() && !data["infs"].as_array().empty()) {
    res = data["infs"][0];
  } else if (data.is_array() && !data.as_array().empty()) {
    res = data[0];
  } else {
    throw std::runtime_error("unexpected servers_and_characters response");
  }
  std::size_t nserv = res["servers"].is_array() ? res["servers"].as_array().size() : 0;
  std::size_t nchar = res["characters"].is_array() ? res["characters"].as_array().size() : 0;
  log().info("found " + std::to_string(nserv) + " servers and " + std::to_string(nchar) +
             " characters");
  response_ = std::move(res);
  return response_;
}

Json AccountInfo::resolve_char(std::string_view name) const {
  if (!response_["characters"].is_array()) return Json(nullptr);
  for (const auto& c : response_["characters"].as_array()) {
    if (c["name"].as_string_opt() == std::optional<std::string>{std::string(name)})
      return c;
  }
  return Json(nullptr);
}

Json AccountInfo::resolve_realm(std::string_view key) const {
  if (!response_["servers"].is_array()) return Json(nullptr);
  for (const auto& s : response_["servers"].as_array()) {
    if (s["key"].as_string_opt() == std::optional<std::string>{std::string(key)})
      return s;
  }
  return Json(nullptr);
}

}  // namespace caracal
