#include "Logger.h"
#include "Reflect/Enum.h"

#include <cstdarg>
#include <cstdio>
#include <array>


#ifdef _WIN32
#include <windows.h>
#define GET_THREAD_ID() ((unsigned long)GetCurrentThreadId())
#else
#include <sys/syscall.h>
#include <unistd.h>
#define GET_THREAD_ID() ((unsigned long)syscall(SYS_gettid))
#endif

namespace caracal {

    namespace Colors {
        //color codes gist here
        //https://gist.github.com/JBlond/2fea43a3049b38287e5e9cefc87b2124
        static constexpr std::array<std::string_view, 6> colors{
            "\033[0m", //reset
            "\033[96m", //blue
            "\033[97m", //white
            "\033[92m", //green
            "\033[93m", //yellow
            "\033[91m", //red
        };
        //static constexpr std::string_view red{"\033[31m]"};
        //static constexpr std::string_view green{"\033[32m]"};
        //static constexpr std::string_view yellow{"\033[33m]"};
        //static constexpr std::string_view reset{"\033[0m]"};
    }

    void InternalPrint(LogType ll, std::string_view format, va_list args) {

        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format.data(), args);
        printf(
            "%s%s%s[%lu]: %s",
            Colors::colors[static_cast<int>(ll)].data(),
            Reflect::Enum::ToString(ll).data(),
            Colors::colors[0].data(),
            GET_THREAD_ID(),
            buffer
        );
        fflush(stdout);
    }

    void Log::Sanity(std::string_view fmt, ...) {
        if (Log::Minimum <= LogType::Sanity) {
            va_list args;
            va_start(args, fmt);
            InternalPrint(LogType::Sanity, fmt, args);
            va_end(args);
        }
    }
    void Log::Normal(std::string_view fmt, ...) {
        if (Log::Minimum <= LogType::Normal) {
            va_list args;
            va_start(args, fmt);
            InternalPrint(LogType::Normal, fmt, args);
            va_end(args);
        }
    }
    void Log::Debug(std::string_view fmt, ...) {
        if (Log::Minimum <= LogType::Debug) {
            va_list args;
            va_start(args, fmt);
            InternalPrint(LogType::Debug, fmt, args);
            va_end(args);
        }
    }
    void Log::Warning(std::string_view fmt, ...) {
        if (Log::Minimum <= LogType::Warning) {
            va_list args;
            va_start(args, fmt);
            InternalPrint(LogType::Warning, fmt, args);
            va_end(args);
        }
    }
    void Log::Error(std::string_view fmt, ...) {
        if (Log::Minimum <= LogType::Error) {
            va_list args;
            va_start(args, fmt);
            InternalPrint(LogType::Error, fmt, args);
            va_end(args);
            //EWE_Debug_Breakpoint();
        }
    }

} // namespace caracal