#include "ipc_storage.hpp"

#include <iterator>

namespace caracal {

    IpcStorage::IpcStorage(std::string ident, Send send) : ident_(std::move(ident)), send_(std::move(send)) {
        Json m = Json::object();
        m["type"] = "stor";
        m["op"] = "init";
        m["ident"] = ident_;
        if (send_) {
            send_(m);
        }
    }

    void IpcStorage::set_item(std::string key, std::string val) {
        items_[key] = val;
        Json data = Json::object();
        data[key] = val;
        Json m = Json::object();
        m["type"] = "stor";
        m["op"] = "set";
        m["ident"] = ident_;
        m["data"] = std::move(data);
        if (send_) {
            send_(m);
        }
    }

    std::optional<std::string> IpcStorage::get_item(const std::string& key) const {
        auto it = items_.find(key);
        if (it == items_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void IpcStorage::remove_item(const std::string& key) {
        items_.erase(key);
        Json data = Json::array();
        data.as_array().push_back(key);
        Json m = Json::object();
        m["type"] = "stor";
        m["op"] = "del";
        m["ident"] = ident_;
        m["data"] = std::move(data);
        if (send_) {
            send_(m);
        }
    }

    void IpcStorage::clear() {
        items_.clear();
        Json m = Json::object();
        m["type"] = "stor";
        m["op"] = "clear";
        m["ident"] = ident_;
        if (send_) {
            send_(m);
        }
    }

    std::optional<std::string> IpcStorage::key(std::size_t n) const {
        if (n >= items_.size()) {
            return std::nullopt;
        }
        auto it = items_.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(n));
        return it->first;
    }

    void IpcStorage::on_message(const Json& m) {
        if (!m.is_object()) {
            return;
        }
        if (m["type"].as_string_opt() != std::optional<std::string>{"stor"}) {
            return;
        }
        if (m["ident"].as_string_opt() != std::optional<std::string>{ident_}) {
            return;
        }
        auto op = m["op"].as_string_opt().value_or("");
        if (op == "set" && m["data"].is_object()) {
            for (const auto& [k, v] : m["data"].as_object()) {
                if (v.is_string()) {
                    items_[k] = v.as_string();
                }
            }
        }
        else if (op == "del" && m["data"].is_array()) {
            for (const auto& k : m["data"].as_array()) {
                if (k.is_string()) {
                    items_.erase(k.as_string());
                }
            }
        }
        else if (op == "clear") {
            items_.clear();
        }
    }

} // namespace caracal
