#pragma once

#include "plugin_abi.hpp"

#include <filesystem>
#include <string>

namespace caracal {

    class PluginHost {
    public:
        PluginHost() = default;
        ~PluginHost();

        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        void load(const std::string& path);
        void unload();
        bool loaded() const { return handle_ != nullptr; }
        const std::string& path() const { return src_path_; }

        // If the plugin file mtime/size changed, swap it in without dropping the session.
        bool reload_if_changed();

        void on_tick(double now_ms);
        void bind(const CaracalApi& api);
    private:
        void load_locked(const std::string& path);

        void* handle_ = nullptr;
        const CaracalPlugin* plugin_ = nullptr;
        CaracalApi api_{};
        std::string src_path_;
        std::string loaded_copy_;
        std::filesystem::file_time_type mtime_{};
        std::uintmax_t size_ = 0;
    };

    std::string resolve_plugin_path(const std::string& name_or_path);

} // namespace caracal
