#pragma once

#include "json.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace caracal {

    class IpcStorage {
    public:
        using Send = std::function<void(const Json&)>;

        IpcStorage(std::string ident, Send send);

        void set_item(std::string key, std::string val);
        std::optional<std::string> get_item(const std::string& key) const;
        void remove_item(const std::string& key);
        void clear();
        std::optional<std::string> key(std::size_t n) const;
        std::size_t length() const { return items_.size(); }
        void on_message(const Json& m);

        const std::string& ident() const { return ident_; }
    private:
        std::string ident_;
        Send send_;
        std::map<std::string, std::string> items_;
    };

} // namespace caracal
