#include "coordinator.hpp"

#include "account_info.hpp"
#include "config_util.hpp"
#include "constants.hpp"
#include "g_data.hpp"
#include "game_files.hpp"
#include "game_session.hpp"
#include "Logger.h"
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

        void on_sig(int) {
            g_stop = true;
        }

        Json load_g_for(const std::string& base_url, const std::string& version) {
            try {
                auto path = game_files::locate_game_file(base_url, "/data.js", version);
                return load_g_data(path);
            } catch (const std::exception& e) {
                Log::Warning("G data unavailable: %s\n", e.what());
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
                    Log::Warning("auth_fail %s %s\n", cfg.cname.c_str(), reason.c_str());
                };
                session.on_connected = [&] { Log::Normal("%s in game\n", cfg.cname.c_str()); };
                try {
                    session.start(cfg, g);
                    session.run_forever(&g_stop, &enabled);
                    session.stop();
                } catch (const std::exception& e) {
                    Log::Error("%s session error: %s\n", cfg.cname.c_str(), e.what());
                    try {
                        session.stop();
                    } catch (...) {}
                }
                if (g_stop || !enabled) {
                    break;
                }
                int wait_ms = 3000;
                if (auth_fail) {
                    ++strikes;
                    wait_ms = std::min(600000, 30000 * (1 << std::min(strikes - 1, 5)));
                }
                else {
                    strikes = 0;
                }
                Log::Warning("restarting %s in %ds\n", cfg.cname.c_str(), wait_ms / 1000);
                auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
                while (!g_stop && enabled && std::chrono::steady_clock::now() < until) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

    } // namespace

    int run_coordinator(const std::string& config_path) {
        std::signal(SIGINT, on_sig);
        std::signal(SIGTERM, on_sig);

        auto cfg = config::interactive(config_path);
        std::string base_url = cfg["base_url"].as_string_opt().value_or("https://adventure.land");
        std::string sess = cfg["session"].as_string_opt().value_or("");
        if (sess.empty()) {
            Log::Error("config.session is empty\n");
            return 1;
        }

        int latest = 0;
        try {
            latest = game_files::ensure_latest(base_url);
            Log::Normal("game files version %d\n", latest);
        } catch (const std::exception& e) {
            Log::Warning("ensure_latest failed: %s\n", e.what());
        }

        AccountInfo acc(base_url, sess);
        Json info;
        try {
            info = acc.update_info();
        } catch (const std::exception& e) {
            Log::Error("account info: %s\n", e.what());
            return 1;
        }

        if (!cfg["characters"].is_object()) {
            Log::Error("config.characters missing\n");
            return 1;
        }

        std::vector<std::thread> threads;
        std::vector<std::unique_ptr<std::atomic<bool>>> flags;
        for (const auto& [name, block] : cfg["characters"].as_object()) {
            if (!block["enabled"].truthy()) {
                continue;
            }
            auto ch = acc.resolve_char(name);
            if (ch.is_null()) {
                Log::Error("unknown character %s\n", name.c_str());
                continue;
            }
            std::string realm_key = block["realm"].as_string_opt().value_or("");
            auto realm = acc.resolve_realm(realm_key);
            if (realm.is_null() && info["servers"].is_array() && !info["servers"].as_array().empty()) {
                realm = info["servers"][0];
                Log::Warning(
                    "realm %s missing, fallback %s\n",
                    realm_key.c_str(),
                    realm["key"].as_string_opt().value_or("?").c_str()
                );
            }
            CharacterConnect cc;
            cc.base_url = base_url;
            cc.session = sess;
            cc.cid = ch["id"].is_number() ? std::to_string(static_cast<long long>(ch["id"].as_number()))
                                          : ch["id"].as_string_opt().value_or("");
            cc.cname = name;
            cc.realm_address = realm["address"].as_string_opt().value_or(realm["addr"].as_string_opt().value_or(""));
            cc.realm_path = realm["path"].as_string_opt().value_or("/socket.io/");
            if (realm["port"].is_number()) {
                cc.realm_port = static_cast<int>(realm["port"].as_number());
            }
            std::string plugin =
                block["plugin"].as_string_opt().value_or(block["script"].as_string_opt().value_or("crabs"));
            if (plugin.find("crabs") != std::string::npos) {
                plugin = "crabs";
            }
            cc.plugin_path = resolve_plugin_path(plugin);

            Json verj = block["version"];
            std::string ver = std::to_string(latest);
            if (!game_files::follows_latest_client_any(verj) && verj.is_number()) {
                ver = std::to_string(static_cast<int>(verj.as_number()));
            }
            else if (verj.is_string() && !verj.as_string().empty() && verj.as_string() != "0") {
                ver = verj.as_string();
            }
            Json g = load_g_for(base_url, ver);

            flags.push_back(std::make_unique<std::atomic<bool>>(true));
            auto* en = flags.back().get();
            threads.emplace_back([cc, g, en] { run_one_character(cc, g, *en); });
            Log::Normal("started %s plugin %s\n", name.c_str(), cc.plugin_path.c_str());
        }

        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        for (auto& f : flags) {
            *f = false;
        }
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        return 0;
    }

} // namespace caracal
