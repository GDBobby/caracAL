#include "realm_rtt.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <netdb.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace caracal {
    namespace {

        int connect_timed(const std::string& host, int port, int timeout_ms, bool tls) {
            addrinfo hints{};
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_family = AF_UNSPEC;
            addrinfo* res = nullptr;
            if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
                return -1;
            }
            int fd = -1;
            for (auto* p = res; p; p = p->ai_next) {
                fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd < 0) {
                    continue;
                }
                timeval tv{};
                tv.tv_sec = timeout_ms / 1000;
                tv.tv_usec = (timeout_ms % 1000) * 1000;
                ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
                    break;
                }
                ::close(fd);
                fd = -1;
            }
            ::freeaddrinfo(res);
            if (fd < 0) {
                return -1;
            }
            if (!tls) {
                return fd;
            }
            SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
            if (!ctx) {
                ::close(fd);
                return -1;
            }
            SSL_CTX_set_default_verify_paths(ctx);
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, fd);
            SSL_set_tlsext_host_name(ssl, host.c_str());
            int ok = SSL_connect(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            if (ok != 1) {
                ::close(fd);
                return -1;
            }
            return fd;
        }

    } // namespace

    RealmHost parse_realm_host(std::string_view realm_host) {
        RealmHost out;
        std::string raw(realm_host);
        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
            raw.pop_back();
        }
        std::size_t b = 0;
        while (b < raw.size() && std::isspace(static_cast<unsigned char>(raw[b]))) {
            ++b;
        }
        raw = raw.substr(b);
        if (raw.empty()) {
            return out;
        }
        auto lower_start = raw;
        for (auto& c : lower_start) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower_start.starts_with("ws://")) {
            raw = raw.substr(5);
        }
        else if (lower_start.starts_with("wss://")) {
            raw = raw.substr(6);
        }
        auto slash = raw.find('/');
        if (slash != std::string::npos) {
            raw = raw.substr(0, slash);
        }
        if (raw.starts_with('[')) {
            auto end = raw.find(']');
            if (end != std::string::npos) {
                out.host = raw.substr(1, end - 1);
                auto rest = raw.substr(end + 1);
                if (rest.starts_with(':')) {
                    try {
                        int n = std::stoi(rest.substr(1));
                        if (n > 0) {
                            out.port = n;
                        }
                    } catch (...) {}
                }
            }
        }
        else {
            auto idx = raw.rfind(':');
            if (idx != std::string::npos && idx > 0 && raw.find(':') == idx) {
                try {
                    int n = std::stoi(raw.substr(idx + 1));
                    if (n > 0) {
                        out.host = raw.substr(0, idx);
                        out.port = n;
                    }
                    else {
                        out.host = raw;
                    }
                } catch (...) {
                    out.host = raw;
                }
            }
            else {
                out.host = raw;
            }
        }
        return out;
    }

    int probe_realm_rtt_ms(const std::string& host, int port, bool tls, int timeout_ms) {
        if (host.empty()) {
            return timeout_ms;
        }
        auto started = std::chrono::steady_clock::now();
        int fd = connect_timed(host, port, timeout_ms, tls);
        if (fd < 0) {
            return timeout_ms;
        }
        ::close(fd);
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        return static_cast<int>(ms);
    }

    int load_deadline_ms_from_rtt(double rtt_ms, int min_ms, int max_ms, int base_ms, int factor) {
        double rtt = (std::isfinite(rtt_ms) && rtt_ms > 0) ? rtt_ms : probe_timeout_ms;
        int v = static_cast<int>(std::ceil(base_ms + factor * rtt));
        if (v < min_ms) {
            v = min_ms;
        }
        if (v > max_ms) {
            v = max_ms;
        }
        return v;
    }

    bool should_refresh_client_for_welcome(double local_version, double welcome_version) {
        if (!std::isfinite(local_version) || !std::isfinite(welcome_version)) {
            return false;
        }
        return welcome_version > local_version;
    }

} // namespace caracal
