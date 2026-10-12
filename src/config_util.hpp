#pragma once

#include "json.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace caracal::config {

    int privileged_port_ceiling();
    int default_web_panel_port();
    bool needs_elevated_bind(int port);

    std::optional<std::string> extract_auth_cookie(const std::vector<std::string>& set_cookie_headers);
    std::optional<std::string> login_success_auth(const Json& data, const std::vector<std::string>& set_cookie);
    std::optional<std::string> make_auth(std::string_view base_url, std::string_view email, std::string_view password);

    Json default_config();
    Json make_config(
        std::string base_url,
        std::string session,
        const Json& characters,
        bool typescript_enabled,
        bool use_bwi,
        bool use_minimap,
        int port,
        std::string title
    );
    std::string make_cfg_string(const Json& conf);
    void write_config(const std::string& path, const Json& conf);
    Json load_config(const std::string& path);
    bool config_exists(const std::string& path);

    // Interactive first-run. Returns loaded or newly written config.
    Json interactive(const std::string& config_path);

} // namespace caracal::config
