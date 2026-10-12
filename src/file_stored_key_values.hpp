#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace caracal {

    class FileStoredKeyValues {
    public:
        explicit FileStoredKeyValues(
            std::string main_path = "garage.jsonl",
            std::string replacer_path = "garage.new.jsonl",
            std::chrono::milliseconds refactor_interval = std::chrono::milliseconds(30'000)
        );
        ~FileStoredKeyValues();

        FileStoredKeyValues(const FileStoredKeyValues&) = delete;
        FileStoredKeyValues& operator=(const FileStoredKeyValues&) = delete;

        FileStoredKeyValues& set(const std::string& key, const std::string& value);
        bool erase(const std::string& key);
        std::optional<std::string> get(const std::string& key) const;
        std::vector<std::pair<std::string, std::string>> entries() const;
        void refactor();
        void close();
        void shutdown();
    private:
        void initialize_handle();
        void append_line(const std::string& line);
        void stop_refactor_thread();

        std::string main_path_;
        std::string replacer_path_;
        std::map<std::string, std::string> backend_;
        mutable std::mutex mu_;
        std::thread refactor_thread_;
        std::chrono::milliseconds interval_;
        bool stop_ = false;
    };

} // namespace caracal
