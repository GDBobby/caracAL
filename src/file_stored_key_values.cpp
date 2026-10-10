#include "file_stored_key_values.hpp"

#include "json.hpp"
#include "log_utils.hpp"

#include <cctype>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace caracal {
namespace fs = std::filesystem;

FileStoredKeyValues::FileStoredKeyValues(std::string main_path, std::string replacer_path,
                                         std::chrono::milliseconds refactor_interval)
    : main_path_(std::move(main_path)),
      replacer_path_(std::move(replacer_path)),
      interval_(refactor_interval) {
  if (main_path_ == replacer_path_) {
    throw std::runtime_error("Please choose different main path and replacer path: " +
                             main_path_);
  }
  initialize_handle();
  refactor_thread_ = std::thread([this] {
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        if (stop_) return;
      }
      std::this_thread::sleep_for(interval_);
      {
        std::unique_lock<std::mutex> lock(mu_);
        if (stop_) return;
      }
      try {
        refactor();
      } catch (...) {
      }
    }
  });
  refactor();
}

FileStoredKeyValues::~FileStoredKeyValues() {
  try {
    close();
  } catch (...) {
  }
}

void FileStoredKeyValues::initialize_handle() {
  if (fs::exists(replacer_path_) && !fs::exists(main_path_)) {
    fs::rename(replacer_path_, main_path_);
  }
  std::ifstream in(main_path_);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    auto trimmed = line;
    std::size_t a = 0;
    while (a < trimmed.size() &&
           std::isspace(static_cast<unsigned char>(trimmed[a])))
      ++a;
    trimmed = trimmed.substr(a);
    if (trimmed.empty()) continue;
    try {
      auto obj = Json::parse(trimmed);
      if (!obj.is_object() || obj.as_object().empty()) continue;
      auto it = obj.as_object().begin();
      if (it->second.is_null()) backend_.erase(it->first);
      else if (it->second.is_string()) backend_[it->first] = it->second.as_string();
      else backend_[it->first] = it->second.dump();
    } catch (const std::exception& err) {
      log().error("FileStoredKeyValues: skipping corrupt line (" +
                  std::string(err.what()) + "): " + trimmed.substr(0, 120));
    }
  }
}

void FileStoredKeyValues::append_line(const std::string& line) {
  std::ofstream out(main_path_, std::ios::app | std::ios::binary);
  out.write(line.data(), static_cast<std::streamsize>(line.size()));
}

void FileStoredKeyValues::refactor() {
  std::lock_guard<std::mutex> lock(mu_);
  std::string blob;
  for (const auto& [k, v] : backend_) {
    Json o = Json::object();
    o[k] = v;
    blob += o.dump();
    blob += '\n';
  }
  {
    std::ofstream out(replacer_path_, std::ios::binary | std::ios::trunc);
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    out.flush();
  }
  int fd = ::open(replacer_path_.c_str(), O_RDONLY);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
  if (fs::exists(main_path_)) fs::remove(main_path_);
  fs::rename(replacer_path_, main_path_);
}

FileStoredKeyValues& FileStoredKeyValues::set(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = backend_.find(key);
  if (it != backend_.end() && it->second == value) return *this;
  Json o = Json::object();
  o[key] = value;
  append_line(o.dump() + "\n");
  backend_[key] = value;
  return *this;
}

bool FileStoredKeyValues::erase(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  Json o = Json::object();
  o[key] = nullptr;
  append_line(o.dump() + "\n");
  return backend_.erase(key) > 0;
}

std::optional<std::string> FileStoredKeyValues::get(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = backend_.find(key);
  if (it == backend_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::pair<std::string, std::string>> FileStoredKeyValues::entries() const {
  std::lock_guard<std::mutex> lock(mu_);
  return {backend_.begin(), backend_.end()};
}

void FileStoredKeyValues::stop_refactor_thread() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  if (refactor_thread_.joinable()) refactor_thread_.join();
}

void FileStoredKeyValues::close() {
  stop_refactor_thread();
}

void FileStoredKeyValues::shutdown() {
  stop_refactor_thread();
  refactor();
}

}  // namespace caracal
