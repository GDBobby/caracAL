#include "log_utils.hpp"

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <unistd.h>

namespace caracal {
namespace {

std::mutex io_mu;
std::optional<Logger> g_log;

int level_rank(LogLevel l) {
  switch (l) {
    case LogLevel::debug:
      return 10;
    case LogLevel::info:
      return 20;
    case LogLevel::warn:
      return 30;
    case LogLevel::error:
      return 40;
    case LogLevel::silent:
    default:
      return 100;
  }
}

const char* level_name(LogLevel l) {
  switch (l) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
    default:
      return "silent";
  }
}

std::string b64_3(const unsigned char* b) {
  static constexpr char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char out[5]{};
  out[0] = tbl[(b[0] >> 2) & 63];
  out[1] = tbl[((b[0] & 3) << 4) | (b[1] >> 4)];
  out[2] = tbl[((b[1] & 15) << 2) | (b[2] >> 6)];
  out[3] = tbl[b[2] & 63];
  return std::string(out, 4);
}

}  // namespace

LogLevel parse_log_level(std::string_view s) {
  if (s == "debug") return LogLevel::debug;
  if (s == "info") return LogLevel::info;
  if (s == "warn") return LogLevel::warn;
  if (s == "error") return LogLevel::error;
  if (s == "silent") return LogLevel::silent;
  return LogLevel::info;
}

std::string git_revision() {
  try {
    std::ifstream head(".git/HEAD");
    if (!head) return "?";
    std::string rev;
    std::getline(head, rev);
    while (!rev.empty() && (rev.back() == '\n' || rev.back() == '\r')) rev.pop_back();
    if (rev.find(':') == std::string::npos) return rev;
    std::ifstream ref(".git/" + rev.substr(5));
    if (!ref) return "?";
    std::string sha;
    std::getline(ref, sha);
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r')) sha.pop_back();
    return sha;
  } catch (...) {
    return "?";
  }
}

std::string process_hash() {
  unsigned char b[3];
  std::random_device rd;
  uint32_t x = rd();
  b[0] = static_cast<unsigned char>(x);
  b[1] = static_cast<unsigned char>(x >> 8);
  b[2] = static_cast<unsigned char>(x >> 16);
  return b64_3(b);
}

Logger::Logger(LogLevel level, Json base) : level_(level), base_(std::move(base)) {
  if (!base_.is_object()) base_ = Json::object();
}

Logger Logger::child(Json extra) const {
  Json merged = base_;
  if (extra.is_object()) {
    for (auto& [k, v] : extra.as_object()) merged[k] = v;
  }
  return Logger(level_, std::move(merged));
}

void Logger::log(LogLevel level, const Json& fields, std::string_view msg) const {
  if (level_rank(level) < level_rank(level_)) return;
  Json rec = base_;
  rec["level"] = level_name(level);
  rec["time"] = static_cast<long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  rec["msg"] = std::string(msg);
  if (fields.is_object()) {
    for (auto& [k, v] : fields.as_object()) rec[k] = v;
  }
  std::lock_guard<std::mutex> lock(io_mu);
  std::cerr << rec.dump() << "\n";
}

void Logger::debug(std::string_view msg, Json fields) const {
  log(LogLevel::debug, fields, msg);
}
void Logger::info(std::string_view msg, Json fields) const {
  log(LogLevel::info, fields, msg);
}
void Logger::warn(std::string_view msg, Json fields) const {
  log(LogLevel::warn, fields, msg);
}
void Logger::error(std::string_view msg, Json fields) const {
  log(LogLevel::error, fields, msg);
}

Logger& log() {
  if (!g_log) g_log.emplace(LogLevel::info, Json::object());
  return *g_log;
}

void set_log(Logger logger) { g_log = std::move(logger); }

void init_logging(LogLevel level) {
  Json base = Json::object();
  base["id"] = process_hash();
  Logger lg(level, base);
  Json fields = Json::object();
  fields["type"] = "log_init";
  fields["pid"] = static_cast<long long>(::getpid());
  fields["cwd"] = std::filesystem::current_path().string();
  fields["git_revision"] = git_revision();
  lg.warn("initializing logging", fields);
  set_log(std::move(lg));
}

}  // namespace caracal
