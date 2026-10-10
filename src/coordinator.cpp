#include "coordinator.hpp"

#include "account_info.hpp"
#include "config_util.hpp"
#include "constants.hpp"
#include "g_data.hpp"
#include "game_files.hpp"
#include "game_session.hpp"
#include "log_utils.hpp"
#include "plugin_host.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>
#include <vector>

namespace caracal {
namespace {

std::atomic<bool> g_stop{false};

void on_sig(int) { g_stop = true; }

Json load_g_for(const std::string& base_url, const std::string& version) {
  try {
    auto path = game_files::locate_game_file(base_url, "/data.js", version);
    return load_g_data(path);
  } catch (const std::exception& e) {
    log().warn(std::string("G data unavailable: ") + e.what());
    return Json(nullptr);
  }
}

void run_one_character(CharacterConnect cfg, Json g, std::atomic<bool>& enabled) {
  int strikes = 0;
  while (!g_stop && enabled) {
    GameSession session;
    bool auth_fail = false;
    session.on_auth_fail = [&](const std::string& reason) {
      auth_fail = true;
      log().warn("auth_fail " + cfg.cname + " " + reason);
    };
    session.on_connected = [&] { log().info(cfg.cname + " in game"); };
    try {
      session.start(cfg, g);
      session.run_forever();
      session.stop();
    } catch (const std::exception& e) {
      log().error(cfg.cname + " session error: " + e.what());
      try {
        session.stop();
      } catch (...) {
      }
    }
    if (g_stop || !enabled) break;
    int wait_ms = 3000;
    if (auth_fail) {
      ++strikes;
      wait_ms = std::min(600000, 30000 * (1 << std::min(strikes - 1, 5)));
    } else {
      strikes = 0;
    }
    log().warn("restarting " + cfg.cname + " in " + std::to_string(wait_ms / 1000) + "s");
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    while (!g_stop && enabled && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

}  // namespace

int run_coordinator(const std::string& config_path) {
  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);

  auto cfg = config::interactive(config_path);
  init_logging(parse_log_level(cfg["log_level"].as_string_opt().value_or("info")));
  std::string base_url = cfg["base_url"].as_string_opt().value_or("https://adventure.land");
  std::string sess = cfg["session"].as_string_opt().value_or("");
  if (sess.empty()) {
    log().error("config.session is empty");
    return 1;
  }

  int latest = 0;
  try {
    latest = game_files::ensure_latest(base_url);
    log().info("game files version " + std::to_string(latest));
  } catch (const std::exception& e) {
    log().warn(std::string("ensure_latest failed: ") + e.what());
  }

  AccountInfo acc(base_url, sess);
  Json info;
  try {
    info = acc.update_info();
  } catch (const std::exception& e) {
    log().error(std::string("account info: ") + e.what());
    return 1;
  }

  if (!cfg["characters"].is_object()) {
    log().error("config.characters missing");
    return 1;
  }

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<std::atomic<bool>>> flags;
  for (const auto& [name, block] : cfg["characters"].as_object()) {
    if (!block["enabled"].truthy()) continue;
    auto ch = acc.resolve_char(name);
    if (ch.is_null()) {
      log().error("unknown character " + name);
      continue;
    }
    std::string realm_key = block["realm"].as_string_opt().value_or("");
    auto realm = acc.resolve_realm(realm_key);
    if (realm.is_null() && info["servers"].is_array() && !info["servers"].as_array().empty()) {
      realm = info["servers"][0];
      log().warn("realm " + realm_key + " missing, fallback " +
                 realm["key"].as_string_opt().value_or("?"));
    }
    CharacterConnect cc;
    cc.base_url = base_url;
    cc.session = sess;
    cc.cid = ch["id"].is_number() ? std::to_string(static_cast<long long>(ch["id"].as_number()))
                                  : ch["id"].as_string_opt().value_or("");
    cc.cname = name;
    cc.realm_address = realm["address"].as_string_opt().value_or(
        realm["addr"].as_string_opt().value_or(""));
    cc.realm_path = realm["path"].as_string_opt().value_or("/socket.io/");
    if (realm["port"].is_number()) cc.realm_port = static_cast<int>(realm["port"].as_number());
    std::string plugin = block["plugin"].as_string_opt().value_or(
        block["script"].as_string_opt().value_or("crabs"));
    if (plugin.find("crabs") != std::string::npos) plugin = "crabs";
    cc.plugin_path = resolve_plugin_path(plugin);

    Json verj = block["version"];
    std::string ver = std::to_string(latest);
    if (!game_files::follows_latest_client_any(verj) && verj.is_number()) {
      ver = std::to_string(static_cast<int>(verj.as_number()));
    } else if (verj.is_string() && !verj.as_string().empty() && verj.as_string() != "0") {
      ver = verj.as_string();
    }
    Json g = load_g_for(base_url, ver);

    flags.push_back(std::make_unique<std::atomic<bool>>(true));
    auto* en = flags.back().get();
    threads.emplace_back([cc, g, en] { run_one_character(cc, g, *en); });
    log().info("started " + name + " plugin " + cc.plugin_path);
  }

  while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (auto& f : flags) *f = false;
  for (auto& t : threads) {
    if (t.joinable()) t.join();
  }
  return 0;
}

}  // namespace caracal
