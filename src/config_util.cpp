#include "config_util.hpp"

#include "account_info.hpp"
#include "constants.hpp"
#include "http_client.hpp"
#include "log_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace caracal::config {
    namespace fs = std::filesystem;

    int privileged_port_ceiling() {
        return constants::privileged_port_ceiling;
    }

    int default_web_panel_port() {
#ifdef _WIN32
        return constants::default_web_panel_port_windows;
#else
        return constants::default_web_panel_port_unix;
#endif
    }

    bool needs_elevated_bind(int port) {
#ifdef _WIN32
        return false;
#else
        return port > 0 && port < privileged_port_ceiling();
#endif
    }

    std::optional<std::string> extract_auth_cookie(const std::vector<std::string>& set_cookie_headers) {
        for (const auto& h : set_cookie_headers) {
            auto pos = h.find("auth=");
            if (pos == std::string::npos) {
                continue;
            }
            auto start = pos + 5;
            auto end = h.find(';', start);
            auto val = h.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!val.empty()) {
                return val;
            }
        }
        return std::nullopt;
    }

    namespace {

        Json find_message(const Json& data) {
            auto has_msg = [](const Json& x) {
                return x.is_object() &&
                       (x.contains("message") || x["type"].as_string_opt() == std::optional<std::string>{"message"});
            };
            if (data.is_array()) {
                for (const auto& x : data.as_array()) {
                    if (has_msg(x)) {
                        return x;
                    }
                }
            }
            else if (data.is_object() && data["infs"].is_array()) {
                for (const auto& x : data["infs"].as_array()) {
                    if (has_msg(x)) {
                        return x;
                    }
                }
            }
            return Json(nullptr);
        }

        std::string prompt_line(std::string_view q, bool hide = false) {
            std::cout << q << std::flush;
            if (hide) {
                // Best-effort; terminals may still echo.
            }
            std::string s;
            std::getline(std::cin, s);
            return s;
        }

        bool prompt_yesno(std::string_view q) {
            for (;;) {
                std::string s = prompt_line(std::string(q) + " [y/N] ");
                if (s.empty() || s == "n" || s == "N" || s == "no") {
                    return false;
                }
                if (s == "y" || s == "Y" || s == "yes") {
                    return true;
                }
            }
        }

    } // namespace

    std::optional<std::string> login_success_auth(const Json& data, const std::vector<std::string>& set_cookie) {
        auto msg = find_message(data);
        if (msg.is_null()) {
            return std::nullopt;
        }
        auto text = msg["message"].as_string_opt().value_or("");
        if (text == "Logged In!") {
            return extract_auth_cookie(set_cookie);
        }
        return std::nullopt;
    }

    std::optional<std::string> make_auth(std::string_view base_url, std::string_view email, std::string_view password) {
        std::string url(base_url);
        while (url.ends_with('/')) {
            url.pop_back();
        }
        url += "/api/signup_or_login";
        Json body = Json::object();
        body["email"] = std::string(email);
        body["password"] = std::string(password);
        body["only_login"] = true;
        auto r = http_post_json(url, body.dump());
        if (r.status < 200 || r.status >= 300) {
            throw std::runtime_error("failed to call login api: " + r.status_text);
        }
        auto data = Json::parse(r.body);
        auto cookies = r.headers_named("set-cookie");
        auto auth = login_success_auth(data, cookies);
        if (!auth) {
            auto msg = find_message(data);
            if (msg.is_null()) {
                throw std::runtime_error("unexpected login api response");
            }
        }
        return auth;
    }

    Json default_config() {
        Json conf = Json::object();
        conf["base_url"] = "https://adventure.land";
        conf["session"] = "1111111111111111-abc123ABCabc123ABCabc";
        conf["cull_versions"] = true;
        conf["enable_TYPECODE"] = false;
        conf["log_level"] = "info";
        Json sinks = Json::array();
        Json rotate = Json::array();
        rotate.as_array().push_back("logrotate");
        rotate.as_array().push_back("./logs/caracAL.log.jsonl");
        rotate.as_array().push_back("--keep");
        rotate.as_array().push_back("3");
        rotate.as_array().push_back("--size");
        rotate.as_array().push_back("4500000");
        sinks.as_array().push_back(rotate);
        conf["log_sinks"] = sinks;
        Json web = Json::object();
        web["title"] = "Bot Controller";
        web["enable_bwi"] = false;
        web["enable_minimap"] = false;
        web["expose_CODE"] = false;
        web["expose_TYPECODE"] = false;
        web["port"] = default_web_panel_port();
        conf["web_app"] = web;
        conf["characters"] = Json::object();
        return conf;
    }

    Json make_config(
        std::string base_url,
        std::string session,
        const Json& characters,
        bool typescript_enabled,
        bool use_bwi,
        bool use_minimap,
        int port,
        std::string title
    ) {
        Json conf = default_config();
        conf["base_url"] = std::move(base_url);
        conf["session"] = std::move(session);
        conf["enable_TYPECODE"] = typescript_enabled;
        conf["characters"] = characters;
        Json web = conf["web_app"];
        web["title"] = std::move(title);
        web["enable_bwi"] = use_bwi;
        web["enable_minimap"] = use_minimap;
        web["port"] = port;
        conf["web_app"] = web;
        return conf;
    }

    std::string make_cfg_string(const Json& conf) {
        return conf.dump(2) + "\n";
    }

    void write_config(const std::string& path, const Json& conf) {
        fs::create_directories(fs::path(path).parent_path().empty() ? "." : fs::path(path).parent_path());
        std::ofstream out(path);
        out << make_cfg_string(conf);
    }

    Json load_config(const std::string& path) {
        std::ifstream in(path);
        std::ostringstream os;
        os << in.rdbuf();
        return Json::parse(os.str());
    }

    bool config_exists(const std::string& path) {
        return fs::exists(path);
    }

    Json interactive(const std::string& config_path) {
        if (config_exists(config_path)) {
            log().info("config file exists. lets get started!");
            return load_config(config_path);
        }
        log().warn("config file does not exist. lets fix that!");
        std::cout << "What servers are you playing on?\n  1) Official (Recommended)\n  2) Custom\n";
        std::string choice = prompt_line("> ");
        std::string base_url = "https://adventure.land";
        if (choice == "2") {
            base_url = prompt_line("Server url (e.g. http://localhost:8083): ");
        }
        std::optional<std::string> session;
        while (!session) {
            std::cout << "please enter your credentials\n";
            auto email = prompt_line("Email: ");
            auto password = prompt_line("Password: ", true);
            session = make_auth(base_url, email, password);
            if (!session) {
                log().warn("email or password seems to be wrong.");
            }
        }
        AccountInfo acc(base_url, *session);
        auto info = acc.update_info();
        Json characters = Json::object();
        std::vector<std::string> names;
        if (info["characters"].is_array()) {
            for (const auto& c : info["characters"].as_array()) {
                auto n = c["name"].as_string_opt();
                if (n) {
                    names.push_back(*n);
                }
            }
        }
        std::string realm = "EUI";
        if (info["servers"].is_array() && !info["servers"].as_array().empty()) {
            realm = info["servers"][0]["key"].as_string_opt().value_or("EUI");
            std::cout << "Available realms:\n";
            for (const auto& s : info["servers"].as_array()) {
                std::cout << "  " << s["key"].as_string_opt().value_or("?") << "\n";
            }
            auto picked = prompt_line("Realm [" + realm + "]: ");
            if (!picked.empty()) {
                realm = picked;
            }
        }
        bool ts = prompt_yesno("Enable TypeScript integration?");
        bool bwi = prompt_yesno("Use the web monitoring panel?");
        bool mini = false;
        int port = default_web_panel_port();
        std::string title = "Bot Controller";
        if (bwi) {
            title = prompt_line("Web panel title [Bot Controller]: ");
            if (title.empty()) {
                title = "Bot Controller";
            }
            mini = prompt_yesno("Enable minimap?");
            auto ps = prompt_line("Web panel port [" + std::to_string(port) + "]: ");
            if (!ps.empty()) {
                port = std::stoi(ps);
            }
            if (needs_elevated_bind(port)) {
                throw std::runtime_error("Ports below 1024 need root on Linux/macOS.");
            }
        }
        std::cout << "Enable characters (comma-separated names, empty = none):\n";
        for (const auto& n : names) {
            std::cout << "  " << n << "\n";
        }
        auto enabled_line = prompt_line("> ");
        std::vector<std::string> enabled;
        {
            std::istringstream is(enabled_line);
            std::string tok;
            while (std::getline(is, tok, ',')) {
                while (!tok.empty() && tok.front() == ' ') {
                    tok.erase(tok.begin());
                }
                while (!tok.empty() && tok.back() == ' ') {
                    tok.pop_back();
                }
                if (!tok.empty()) {
                    enabled.push_back(tok);
                }
            }
        }
        for (const auto& n : names) {
            Json c = Json::object();
            c["realm"] = realm;
            c["enabled"] = std::find(enabled.begin(), enabled.end(), n) != enabled.end();
            c["version"] = 0;
            if (ts) {
                c["typescript"] = "caracAL/examples/crabs_with_tophats.js";
            }
            else {
                c["script"] = "caracAL/examples/crabs.js";
            }
            characters[n] = c;
        }
        auto conf = make_config(base_url, *session, characters, ts, bwi, mini, port, title);
        write_config(config_path, conf);
        log().info("config file created. lets get started!");
        return conf;
    }

} // namespace caracal::config
