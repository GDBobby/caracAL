#pragma once

#include <string_view>

namespace caracal {

    enum class LogType {
        None, //dont print anything directly to this
        Sanity, //lowest level, use in situations where it SHOULD work
        Normal, //for stuff like "device was created"
        Debug,
        Warning, //expected to easily break
        Error, //something already broke
    };

    struct Log {

        static constexpr LogType Minimum = LogType::Sanity;
        inline static bool Enabled = true;

        static void Sanity(std::string_view fmt, ...);
        static void Normal(std::string_view fmt, ...);
        static void Debug(std::string_view fmt, ...);
        static void Warning(std::string_view fmt, ...);
        static void Error(std::string_view fmt, ...);
    };

} // namespace caracal
