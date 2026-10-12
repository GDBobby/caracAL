#include "character_thread.hpp"

#include "game_files.hpp"
#include "html_vars.hpp"
#include "string_util.hpp"

#include <stdexcept>

namespace caracal::character {
    namespace {

        std::string json_str(const Json& j, const char* key, std::string def = {}) {
            auto v = j[key].as_string_opt();
            return v ? *v : def;
        }

    } // namespace

    ProcessArgs parse_process_args(const Json& msg) {
        Json a = msg;
        if (msg.is_object() && msg["arguments"].is_object()) {
            a = msg["arguments"];
        }
        ProcessArgs p;
        p.cname = json_str(a, "cname");
        if (a["clid"].is_number()) {
            p.clid = static_cast<int>(a["clid"].as_number());
        }
        p.base_url = json_str(a, "base_url");
        p.sess = json_str(a, "sess");
        p.cid = json_str(a, "cid");
        p.realm_address = json_str(a, "realm_address");
        if (p.realm_address.empty()) {
            p.realm_address = json_str(a, "realm_addr");
        }
        p.realm_path = json_str(a, "realm_path");
        if (a["realm_port"].is_number()) {
            p.realm_port = static_cast<int>(a["realm_port"].as_number());
        }
        if (a["version"].is_number()) {
            p.version = std::to_string(static_cast<long long>(a["version"].as_number()));
        }
        else {
            p.version = json_str(a, "version");
        }
        p.script_file = json_str(a, "script_file");
        if (p.script_file.empty()) {
            p.script_file = json_str(a, "script");
        }
        p.track_latest = a["track_latest"].truthy();
        return p;
    }

    std::string server_address(const ProcessArgs& args, std::string_view realm_host) {
        if (realm_host.empty()) {
            throw std::runtime_error("realm address missing from servers_and_characters (expected address/addr)");
        }
        std::string host(realm_host);
        auto low = to_lower(host);
        if (low.starts_with("ws://") || low.starts_with("wss://")) {
            return host;
        }
        if (args.base_url.starts_with("http://")) {
            return "ws://" + host;
        }
        return "wss://" + host;
    }

    int probe_port(const ProcessArgs& args, std::optional<int> parsed_port) {
        if (args.realm_port) {
            return *args.realm_port;
        }
        if (parsed_port) {
            return *parsed_port;
        }
        return args.base_url.starts_with("http://") ? 80 : 443;
    }

    bool probe_tls(const ProcessArgs& args) {
        return !args.base_url.starts_with("http://");
    }

    std::vector<std::string> game_source_files(const ProcessArgs& args) {
        std::vector<std::string> out;
        auto globals = game_files::resolve_html_globals_source(args.base_url, args.version);
        if (!globals.empty()) {
            out.push_back(globals);
        }
        for (auto& f : game_files::get_game_files(args.base_url, args.version)) {
            out.push_back(game_files::locate_game_file(args.base_url, f, args.version));
        }
        out.emplace_back("html_vars"); // embedded: html_vars_source
        (void)html_vars_source;
        return out;
    }

    std::string target_script(const ProcessArgs& args) {
        return "./CODE/" + args.script_file;
    }

    bool is_auth_reject_text(std::string_view s) {
        auto t = to_lower(std::string(s));
        return t.find("authentication_failed") != std::string::npos ||
               t.find("characters_unconfirmed") != std::string::npos ||
               t.find("failed: password_issue") != std::string::npos || t.find("failed: ingame") != std::string::npos ||
               t.find("wrong passphrase") != std::string::npos ||
               t.find("authorization_in_progress") != std::string::npos ||
               t.find("authorization in progress") != std::string::npos;
    }

    bool is_auth_reject_payload(const Json& data) {
        if (data.is_null()) {
            return false;
        }
        if (data.is_string()) {
            return is_auth_reject_text(data.as_string());
        }
        if (data.is_object()) {
            auto phrase = data["phrase"].as_string_opt().value_or("");
            auto message = data["message"].as_string_opt().value_or(data["reason"].as_string_opt().value_or(""));
            return is_auth_reject_text(phrase) || is_auth_reject_text(message);
        }
        return false;
    }

    Json msg_process_ready() {
        Json m = Json::object();
        m["type"] = "process_ready";
        return m;
    }
    Json msg_initialized() {
        Json m = Json::object();
        m["type"] = "initialized";
        return m;
    }
    Json msg_connected() {
        Json m = Json::object();
        m["type"] = "connected";
        return m;
    }
    Json msg_deploy(
        std::optional<std::string> character,
        std::optional<std::string> realm,
        std::optional<std::string> script,
        std::optional<std::string> version
    ) {
        Json m = Json::object();
        m["type"] = "deploy";
        if (character) {
            m["character"] = *character;
        }
        if (realm) {
            m["realm"] = *realm;
        }
        if (script) {
            m["script"] = *script;
        }
        if (version) {
            m["version"] = *version;
        }
        return m;
    }
    Json msg_shutdown(std::optional<std::string> character) {
        Json m = Json::object();
        m["type"] = "shutdown";
        if (character) {
            m["character"] = *character;
        }
        return m;
    }
    Json msg_auth_fail(std::string_view reason) {
        Json m = Json::object();
        m["type"] = "auth_fail";
        m["reason"] = std::string(reason);
        return m;
    }
    Json msg_game_client_check(std::string_view reason) {
        Json m = Json::object();
        m["type"] = "game_client_check";
        m["reason"] = std::string(reason);
        return m;
    }

} // namespace caracal::character
