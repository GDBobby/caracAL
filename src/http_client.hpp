#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace caracal {

    struct Url {
        std::string scheme;
        std::string host;
        std::string path;
        int port = 0;
        bool tls = false;
    };

    struct HttpResponse {
        int status = 0;
        std::string status_text;
        std::string body;
        std::vector<std::pair<std::string, std::string>> headers;

        std::optional<std::string> header(std::string_view name) const;
        std::vector<std::string> headers_named(std::string_view name) const;
    };

    Url parse_url(std::string_view url);
    HttpResponse http_request(
        std::string method,
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::string_view body = {},
        int timeout_ms = 15000
    );

    inline HttpResponse http_get(std::string_view url) {
        return http_request("GET", url);
    }

    inline HttpResponse http_post_json(
        std::string_view url,
        std::string_view json,
        const std::vector<std::pair<std::string, std::string>>& extra = {}
    ) {
        auto headers = extra;
        headers.emplace_back("Content-Type", "application/json");
        headers.emplace_back("Accept", "application/json");
        return http_request("POST", url, headers, json);
    }

} // namespace caracal
