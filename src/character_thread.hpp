#pragma once

#include "json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace caracal::character {

struct ProcessArgs {
  std::string cname;
  int clid = 0;
  std::string base_url;
  std::string sess;
  std::string cid;
  std::string realm_address;
  std::string realm_path;
  std::optional<int> realm_port;
  std::string version;
  std::string script_file;
  std::string typescript_file;
  bool enable_map = false;
  bool track_latest = false;
};

ProcessArgs parse_process_args(const Json& msg);
std::string server_address(const ProcessArgs& args, std::string_view realm_host);
int probe_port(const ProcessArgs& args, std::optional<int> parsed_port);
bool probe_tls(const ProcessArgs& args);
std::vector<std::string> game_source_files(const ProcessArgs& args);
std::string target_script(const ProcessArgs& args);
bool is_auth_reject_payload(const Json& data);
bool is_auth_reject_text(std::string_view s);

Json msg_process_ready();
Json msg_initialized();
Json msg_connected();
Json msg_deploy(std::optional<std::string> character = {},
                std::optional<std::string> realm = {},
                std::optional<std::string> script = {},
                std::optional<std::string> version = {});
Json msg_shutdown(std::optional<std::string> character = {});
Json msg_auth_fail(std::string_view reason);
Json msg_game_client_check(std::string_view reason);

}  // namespace caracal::character
