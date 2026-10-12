#include "socket_io.hpp"

#include "log_utils.hpp"

#include <cctype>
#include <stdexcept>

namespace caracal {

std::string encode_engine_message(std::string_view socket_io_payload) {
  return "4" + std::string(socket_io_payload);
}

std::string encode_socket_event(std::string_view event, const Json& data) {
  Json arr = Json::array();
  arr.as_array().push_back(std::string(event));
  if (!data.is_null()) arr.as_array().push_back(data);
  return encode_engine_message("2" + arr.dump());
}

bool parse_engine_open(std::string_view raw, Json& out) {
  if (raw.empty() || raw[0] != '0') return false;
  try {
    out = Json::parse(raw.substr(1));
    return true;
  } catch (...) {
    return false;
  }
}

std::optional<SocketIoPacket> parse_engine_packet(std::string_view raw) {
  if (raw.empty()) return std::nullopt;
  char eio = raw[0];
  if (eio == '2' || eio == '3' || eio == '1' || eio == '6') return std::nullopt;
  if (eio != '4') return std::nullopt;
  auto sio = raw.substr(1);
  if (sio.empty()) return std::nullopt;
  // 2 = event, optional ack id digits then JSON array
  if (sio[0] != '2') return std::nullopt;
  std::size_t i = 1;
  while (i < sio.size() && std::isdigit(static_cast<unsigned char>(sio[i]))) ++i;
  try {
    auto arr = Json::parse(sio.substr(i));
    if (!arr.is_array() || arr.as_array().empty()) return std::nullopt;
    SocketIoPacket p;
    p.event = arr[0].as_string_opt().value_or("");
    if (arr.as_array().size() > 1) p.data = arr[1];
    return p;
  } catch (...) {
    return std::nullopt;
  }
}

void SocketIo::connect(std::string_view ws_url, int timeout_ms) {
  ws_.connect(ws_url, timeout_ms);
  auto opened = ws_.poll_text(timeout_ms);
  if (!opened) throw std::runtime_error("engine.io open timeout");
  Json info;
  if (parse_engine_open(*opened, info)) {
    if (info["pingInterval"].is_number()) {
      ping_interval_ms_ = static_cast<int>(info["pingInterval"].as_number());
    }
  }
  ws_.send_text("40");
  auto ns = ws_.poll_text(timeout_ms);
  if (!ns) return;
  if (ns->starts_with("40")) return;
  handle_engine(*ns, pending_);
}

void SocketIo::emit(std::string_view event, const Json& data) {
  ws_.send_text(encode_socket_event(event, data));
}

void SocketIo::disconnect() {
  if (!ws_.alive()) return;
  try {
    ws_.send_text("41");
  } catch (...) {
  }
  try {
    ws_.send_text("1");
  } catch (...) {
  }
  try {
    ws_.send_close();
  } catch (...) {
  }
}

void SocketIo::close() {
  disconnect();
  ws_.close();
}

void SocketIo::handle_engine(std::string_view raw, std::optional<SocketIoPacket>& out) {
  if (raw.empty()) return;
  if (raw[0] == '2') {
    ws_.send_text("3");
    return;
  }
  if (raw[0] == '3' || raw[0] == '6') return;
  if (raw[0] == '1') throw std::runtime_error("engine.io close");
  out = parse_engine_packet(raw);
}

std::optional<SocketIoPacket> SocketIo::poll(int timeout_ms) {
  if (pending_) {
    auto p = std::move(pending_);
    pending_.reset();
    return p;
  }
  auto raw = ws_.poll_text(timeout_ms);
  if (!raw) return std::nullopt;
  std::optional<SocketIoPacket> out;
  handle_engine(*raw, out);
  return out;
}

}  // namespace caracal
