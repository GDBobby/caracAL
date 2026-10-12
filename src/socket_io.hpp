#pragma once

#include "json.hpp"
#include "websocket.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace caracal {

struct SocketIoPacket {
  std::string event;
  Json data = Json(nullptr);
};

std::string encode_engine_message(std::string_view socket_io_payload);
std::string encode_socket_event(std::string_view event, const Json& data);
bool parse_engine_open(std::string_view raw, Json& out);
std::optional<SocketIoPacket> parse_engine_packet(std::string_view raw);

class SocketIo {
 public:
  void connect(std::string_view ws_url, int timeout_ms = 15000);
  void emit(std::string_view event, const Json& data = Json(nullptr));
  void disconnect();
  void close();
  bool alive() const { return ws_.alive(); }

  std::optional<SocketIoPacket> poll(int timeout_ms);

 private:
  void handle_engine(std::string_view raw, std::optional<SocketIoPacket>& out);

  WebSocket ws_;
  int ping_interval_ms_ = 25000;
  std::optional<SocketIoPacket> pending_;
};

}  // namespace caracal
