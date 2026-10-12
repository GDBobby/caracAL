#include "plugin_host.hpp"

#include "log_utils.hpp"

#include <dlfcn.h>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unistd.h>

namespace caracal {
    namespace fs = std::filesystem;

    namespace {

        std::string copy_for_load(const std::string& src) {
            fs::path in(src);
            auto dest =
                fs::temp_directory_path() / ("caracal-plugin-" + std::to_string(::getpid()) + "-" +
                                             in.filename().string() + "-" + std::to_string(std::random_device{}()));
            fs::copy_file(in, dest, fs::copy_options::overwrite_existing);
            return dest.string();
        }

    } // namespace

    PluginHost::~PluginHost() {
        try {
            unload();
        } catch (...) {}
    }

    void PluginHost::bind(const CaracalApi& api) {
        api_ = api;
    }

    void PluginHost::load(const std::string& path) {
        unload();
        src_path_ = path;
        load_locked(path);
    }

    void PluginHost::load_locked(const std::string& path) {
        auto st = fs::status(path);
        if (!fs::exists(st)) {
            throw std::runtime_error("plugin not found: " + path);
        }
        mtime_ = fs::last_write_time(path);
        size_ = fs::file_size(path);
        loaded_copy_ = copy_for_load(path);
        handle_ = ::dlopen(loaded_copy_.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) {
            throw std::runtime_error(std::string("dlopen failed: ") + ::dlerror());
        }
        auto sym = reinterpret_cast<const CaracalPlugin* (*)()>(::dlsym(handle_, "caracal_plugin"));
        if (!sym) {
            ::dlclose(handle_);
            handle_ = nullptr;
            throw std::runtime_error("plugin missing caracal_plugin() export");
        }
        plugin_ = sym();
        if (!plugin_ || plugin_->abi != CARACAL_PLUGIN_ABI) {
            ::dlclose(handle_);
            handle_ = nullptr;
            plugin_ = nullptr;
            throw std::runtime_error("incompatible plugin ABI");
        }
        api_.abi = CARACAL_PLUGIN_ABI;
        if (plugin_->on_load) {
            plugin_->on_load(&api_);
        }
        log().info("loaded plugin " + std::string(plugin_->name ? plugin_->name : path), Json::object());
    }

    void PluginHost::unload() {
        if (plugin_ && plugin_->on_unload) {
            try {
                plugin_->on_unload();
            } catch (...) {}
        }
        if (handle_) {
            ::dlclose(handle_);
            handle_ = nullptr;
        }
        plugin_ = nullptr;
        if (!loaded_copy_.empty()) {
            std::error_code ec;
            fs::remove(loaded_copy_, ec);
            loaded_copy_.clear();
        }
    }

    bool PluginHost::reload_if_changed() {
        if (src_path_.empty() || !fs::exists(src_path_)) {
            return false;
        }
        auto mt = fs::last_write_time(src_path_);
        auto sz = fs::file_size(src_path_);
        if (mt == mtime_ && sz == size_) {
            return false;
        }
        log().info("reloading plugin " + src_path_);
        auto api = api_;
        unload();
        api_ = api;
        load_locked(src_path_);
        return true;
    }

    void PluginHost::on_tick(double now_ms) {
        if (plugin_ && plugin_->on_tick) {
            plugin_->on_tick(now_ms);
        }
    }

    std::string resolve_plugin_path(const std::string& name_or_path) {
        fs::path p(name_or_path);
        if (p.has_parent_path() || p.extension() == ".so" || p.extension() == ".dll") {
            return name_or_path;
        }
        const char* dirs[] = {"./plugins", "./build/plugins", "./build", "."};
        std::string names[] =
            {"lib" + name_or_path + ".so", name_or_path + ".so", "lib" + name_or_path + ".dll", name_or_path + ".dll"};
        for (auto* d : dirs) {
            for (auto& n : names) {
                fs::path cand = fs::path(d) / n;
                if (fs::exists(cand)) {
                    return cand.string();
                }
            }
        }
        return (fs::path("./plugins") / ("lib" + name_or_path + ".so")).string();
    }

} // namespace caracal
