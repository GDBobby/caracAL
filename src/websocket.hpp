#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace caracal {

    std::string websocket_mask_key_b64(const unsigned char key[16]);
    std::vector<std::uint8_t> encode_ws_frame(std::string_view payload, bool binary = false);
    bool decode_ws_frame(
        const std::vector<std::uint8_t>& buf,
        std::size_t& used,
        int& opcode,
        std::string& payload,
        bool& fin
    );

    class WebSocket {
    public:
        WebSocket() = default;
        ~WebSocket();
        WebSocket(const WebSocket&) = delete;
        WebSocket& operator=(const WebSocket&) = delete;

        void connect(std::string_view url, int timeout_ms = 15000);
        void send_text(std::string_view msg);
        void send_close();
        void close();
        bool alive() const { return fd_ >= 0; }

        // Returns a complete text message, std::nullopt on timeout. Throws on close/error.
        std::optional<std::string> poll_text(int timeout_ms);
    private:
        void write_all(std::string_view data);
        std::string read_some();
        int wait_readable(int timeout_ms);
        void send_frame(std::string_view payload, int opcode);

        int fd_ = -1;
        void* ssl_ = nullptr;
        void* ssl_ctx_ = nullptr;
        std::vector<std::uint8_t> inbuf_;
    };

} // namespace caracal
