#pragma once

#include <map>
#include <string>
#include <string_view>

#include "json.hpp"

namespace caracal {

    enum class LogLevel {
        silent,
        debug,
        info,
        warn,
        error
    };

    LogLevel parse_log_level(std::string_view s);
    std::string git_revision();
    std::string process_hash();

    class Logger {
    public:
        explicit Logger(LogLevel level = LogLevel::info, Json base = Json::object());

        Logger child(Json extra) const;

        void log(LogLevel level, const Json& fields, std::string_view msg) const;
        void debug(std::string_view msg, Json fields = Json::object()) const;
        void info(std::string_view msg, Json fields = Json::object()) const;
        void warn(std::string_view msg, Json fields = Json::object()) const;
        void error(std::string_view msg, Json fields = Json::object()) const;

        LogLevel level() const { return level_; }
    private:
        LogLevel level_;
        Json base_;
    };

    Logger& log();
    void set_log(Logger logger);
    void init_logging(LogLevel level);

    inline const std::map<std::string, int> ctype_to_clid = {
        {"merchant", 1},
        {"warrior", 2},
        {"paladin", 3},
        {"priest", 4},
        {"ranger", 5},
        {"rogue", 6},
        {"mage", 7},
    };

} // namespace caracal
