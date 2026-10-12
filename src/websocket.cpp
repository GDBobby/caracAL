#include "websocket.hpp"

#include "http_client.hpp"

#include <chrono>
#include <openssl/ssl.h>
#include <poll.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace caracal {
    namespace {

        std::string b64(const unsigned char* p, std::size_t n) {
            static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            for (std::size_t i = 0; i < n; i += 3) {
                unsigned a = p[i];
                unsigned b = i + 1 < n ? p[i + 1] : 0;
                unsigned c = i + 2 < n ? p[i + 2] : 0;
                unsigned triple = (a << 16) | (b << 8) | c;
                out.push_back(tbl[(triple >> 18) & 63]);
                out.push_back(tbl[(triple >> 12) & 63]);
                out.push_back(i + 1 < n ? tbl[(triple >> 6) & 63] : '=');
                out.push_back(i + 2 < n ? tbl[triple & 63] : '=');
            }
            return out;
        }

        void apply_mask(std::uint8_t* data, std::size_t n, const std::uint8_t mask[4]) {
            for (std::size_t i = 0; i < n; ++i) {
                data[i] ^= mask[i & 3];
            }
        }

    } // namespace

    std::string websocket_mask_key_b64(const unsigned char key[16]) {
        return b64(key, 16);
    }

    std::vector<std::uint8_t> encode_ws_frame(std::string_view payload, bool binary) {
        std::vector<std::uint8_t> out;
        out.push_back(static_cast<std::uint8_t>(0x80 | (binary ? 2 : 1)));
        std::uint8_t mask[4];
        std::random_device rd;
        uint32_t r = rd();
        mask[0] = r;
        mask[1] = r >> 8;
        mask[2] = r >> 16;
        mask[3] = r >> 24;
        auto n = payload.size();
        if (n < 126) {
            out.push_back(static_cast<std::uint8_t>(0x80 | n));
        }
        else if (n <= 0xFFFF) {
            out.push_back(0x80 | 126);
            out.push_back(static_cast<std::uint8_t>(n >> 8));
            out.push_back(static_cast<std::uint8_t>(n));
        }
        else {
            out.push_back(0x80 | 127);
            for (int i = 7; i >= 0; --i) {
                out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
            }
        }
        out.insert(out.end(), mask, mask + 4);
        auto start = out.size();
        out.insert(out.end(), payload.begin(), payload.end());
        apply_mask(out.data() + start, payload.size(), mask);
        return out;
    }

    bool decode_ws_frame(
        const std::vector<std::uint8_t>& buf,
        std::size_t& used,
        int& opcode,
        std::string& payload,
        bool& fin
    ) {
        used = 0;
        if (buf.size() < 2) {
            return false;
        }
        fin = (buf[0] & 0x80) != 0;
        opcode = buf[0] & 0x0F;
        bool masked = (buf[1] & 0x80) != 0;
        std::uint64_t len = buf[1] & 0x7F;
        std::size_t i = 2;
        if (len == 126) {
            if (buf.size() < 4) {
                return false;
            }
            len = (static_cast<std::uint64_t>(buf[2]) << 8) | buf[3];
            i = 4;
        }
        else if (len == 127) {
            if (buf.size() < 10) {
                return false;
            }
            len = 0;
            for (int k = 0; k < 8; ++k) {
                len = (len << 8) | buf[2 + k];
            }
            i = 10;
        }
        std::uint8_t mask[4]{};
        if (masked) {
            if (buf.size() < i + 4) {
                return false;
            }
            for (int k = 0; k < 4; ++k) {
                mask[k] = buf[i + k];
            }
            i += 4;
        }
        if (buf.size() < i + len) {
            return false;
        }
        payload.assign(reinterpret_cast<const char*>(buf.data() + i), static_cast<std::size_t>(len));
        if (masked) {
            for (std::size_t k = 0; k < payload.size(); ++k) {
                payload[k] = static_cast<char>(static_cast<std::uint8_t>(payload[k]) ^ mask[k & 3]);
            }
        }
        used = i + static_cast<std::size_t>(len);
        return true;
    }

    WebSocket::~WebSocket() {
        close();
    }

    void WebSocket::write_all(std::string_view data) {
        auto* ssl = static_cast<SSL*>(ssl_);
        std::size_t off = 0;
        while (off < data.size()) {
            ssize_t n = 0;
            if (ssl) {
                n = SSL_write(ssl, data.data() + off, static_cast<int>(data.size() - off));
            }
            else {
                n = ::send(fd_, data.data() + off, data.size() - off, 0);
            }
            if (n <= 0) {
                throw std::runtime_error("websocket write failed");
            }
            off += static_cast<std::size_t>(n);
        }
    }

    std::string WebSocket::read_some() {
        char buf[4096];
        auto* ssl = static_cast<SSL*>(ssl_);
        int n = 0;
        if (ssl) {
            n = SSL_read(ssl, buf, sizeof(buf));
        }
        else {
            n = static_cast<int>(::recv(fd_, buf, sizeof(buf), 0));
        }
        if (n <= 0) {
            return {};
        }
        return {buf, static_cast<std::size_t>(n)};
    }

    int WebSocket::wait_readable(int timeout_ms) {
        pollfd p{};
        p.fd = fd_;
        p.events = POLLIN;
        return ::poll(&p, 1, timeout_ms);
    }

    void WebSocket::send_frame(std::string_view payload, int opcode) {
        auto frame = encode_ws_frame(payload, opcode == 2);
        if (opcode != 1 && opcode != 2) {
            // rebuild with requested opcode
            if (!frame.empty()) {
                frame[0] = static_cast<std::uint8_t>(0x80 | (opcode & 0x0F));
            }
        }
        write_all(std::string_view(reinterpret_cast<char*>(frame.data()), frame.size()));
    }

    void WebSocket::connect(std::string_view url, int timeout_ms) {
        close();
        auto u = parse_url(url);
        fd_ = connect_tcp(u.host, u.port, timeout_ms);
        if (u.tls) {
            static bool once = [] {
                SSL_library_init();
                SSL_load_error_strings();
                return true;
            }();
            (void)once;
            auto* ctx = SSL_CTX_new(TLS_client_method());
            if (!ctx) {
                throw std::runtime_error("SSL_CTX_new failed");
            }
            SSL_CTX_set_default_verify_paths(ctx);
            auto* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, fd_);
            SSL_set_tlsext_host_name(ssl, u.host.c_str());
            SSL_set1_host(ssl, u.host.c_str());
            if (SSL_connect(ssl) != 1) {
                SSL_free(ssl);
                SSL_CTX_free(ctx);
                throw std::runtime_error("tls handshake failed: " + u.host);
            }
            ssl_ = ssl;
            ssl_ctx_ = ctx;
        }
        unsigned char key[16];
        std::random_device rd;
        for (int i = 0; i < 16; ++i) {
            key[i] = static_cast<unsigned char>(rd());
        }
        std::ostringstream req;
        req << "GET " << u.path << " HTTP/1.1\r\n";
        req << "Host: " << u.host;
        if ((u.tls && u.port != 443) || (!u.tls && u.port != 80)) {
            req << ":" << u.port;
        }
        req << "\r\n";
        req << "Upgrade: websocket\r\nConnection: Upgrade\r\n";
        req << "Sec-WebSocket-Key: " << b64(key, 16) << "\r\n";
        req << "Sec-WebSocket-Version: 13\r\n";
        req << "Origin: https://" << u.host << "\r\n";
        req << "\r\n";
        write_all(req.str());
        std::string raw;
        while (raw.find("\r\n\r\n") == std::string::npos) {
            auto c = read_some();
            if (c.empty()) {
                throw std::runtime_error("websocket handshake closed");
            }
            raw += c;
            if (raw.size() > 65536) {
                throw std::runtime_error("websocket handshake too large");
            }
        }
        if (raw.find("101") == std::string::npos) {
            throw std::runtime_error("websocket upgrade failed: " + raw.substr(0, 120));
        }
        auto rest = raw.substr(raw.find("\r\n\r\n") + 4);
        inbuf_.assign(rest.begin(), rest.end());
    }

    void WebSocket::send_text(std::string_view msg) {
        send_frame(msg, 1);
    }

    void WebSocket::send_close() {
        if (fd_ < 0) {
            return;
        }
        try {
            send_frame({}, 8);
        } catch (...) {}
    }

    std::optional<std::string> WebSocket::poll_text(int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        std::string acc;
        int acc_op = -1;
        while (true) {
            std::size_t used = 0;
            int opcode = 0;
            std::string payload;
            bool fin = false;
            if (!decode_ws_frame(inbuf_, used, opcode, payload, fin)) {
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    return std::nullopt;
                }
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
                if (wait_readable(static_cast<int>(left)) <= 0) {
                    return std::nullopt;
                }
                auto chunk = read_some();
                if (chunk.empty()) {
                    throw std::runtime_error("websocket closed");
                }
                inbuf_.insert(inbuf_.end(), chunk.begin(), chunk.end());
                continue;
            }
            inbuf_.erase(inbuf_.begin(), inbuf_.begin() + static_cast<std::ptrdiff_t>(used));
            if (opcode == 8) {
                throw std::runtime_error("websocket close frame");
            }
            if (opcode == 9) {
                send_frame(payload, 10);
                continue;
            }
            if (opcode == 10) {
                continue;
            }
            if (opcode == 0) {
                acc += payload;
            }
            else if (opcode == 1 || opcode == 2) {
                acc = payload;
                acc_op = opcode;
            }
            if (fin && acc_op >= 0) {
                return acc;
            }
        }
    }

    void WebSocket::close() {
        send_close();
        if (ssl_) {
            auto* ssl = static_cast<SSL*>(ssl_);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl_ = nullptr;
        }
        if (ssl_ctx_) {
            SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
            ssl_ctx_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        inbuf_.clear();
    }

} // namespace caracal
