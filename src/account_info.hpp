#pragma once

#include "json.hpp"

#include <string>
#include <string_view>

namespace caracal {

    class AccountInfo {
    public:
        AccountInfo(std::string base_url, std::string session);

        Json update_info();
        const Json& response() const { return response_; }
        Json resolve_char(std::string_view name) const;
        Json resolve_realm(std::string_view key) const;

        const std::string& base_url() const { return base_url_; }
        const std::string& session() const { return session_; }
    private:
        std::string base_url_;
        std::string session_;
        Json response_ = Json::object();
    };

} // namespace caracal
