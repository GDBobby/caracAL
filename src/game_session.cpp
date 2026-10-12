#include "game_session.hpp"

#include "Logger.h"
#include "character_thread.hpp"
#include "http_client.hpp"
#include "log_utils.hpp"
#include "monitoring_util.hpp"
#include "pathing.hpp"
#include "realm_rtt.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace caracal {
    namespace {

        double clock_ms() {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch()
            )
                                           .count());
        }

        void ingest_map(std::unordered_map<std::string, Entity>& dest, const Json& node) {
            if (node.is_array()) {
                for (const auto& e : node.as_array()) {
                    auto ent = entity_from_json(e);
                    if (!ent.id.empty()) {
                        dest[ent.id] = std::move(ent);
                    }
                }
            }
            else if (node.is_object()) {
                for (const auto& [id, e] : node.as_object()) {
                    dest[id] = entity_from_json(e, id);
                }
            }
        }

    } // namespace

    const CaracalEntity* GameSession::api_self(void* ctx) {
        auto* s = static_cast<GameSession*>(ctx);
        s->fill_view(s->self_view_, s->self_);
        s->self_id_store_ = s->self_.id;
        s->self_type_store_ = s->self_.type;
        s->self_name_store_ = s->self_.name;
        s->self_view_.id = s->self_id_store_.c_str();
        s->self_view_.type = s->self_type_store_.c_str();
        s->self_view_.name = s->self_name_store_.c_str();
        if (s->smart_.active) {
            s->self_view_.going_x = s->smart_.dest_x;
            s->self_view_.going_y = s->smart_.dest_y;
        }
        return &s->self_view_;
    }

    const CaracalEntity* GameSession::api_nearest(void* ctx, const char* type) {
        auto* s = static_cast<GameSession*>(ctx);
        const Entity* best = nullptr;
        double best_d = 1e100;
        std::string want = type ? type : "";
        for (auto& [_, m] : s->monsters_) {
            if (!want.empty() && !entity_matches_type(m, want)) {
                continue;
            }
            double d = simple_distance(s->self_.x, s->self_.y, m.x, m.y);
            if (d < best_d) {
                best_d = d;
                best = &m;
            }
        }
        if (!best) {
            return nullptr;
        }
        s->fill_view(s->nearest_view_, *best);
        s->nearest_id_store_ = best->id;
        s->nearest_type_store_ = best->type;
        s->nearest_name_store_ = best->name;
        s->nearest_view_.id = s->nearest_id_store_.c_str();
        s->nearest_view_.type = s->nearest_type_store_.c_str();
        s->nearest_view_.name = s->nearest_name_store_.c_str();
        return &s->nearest_view_;
    }

    int GameSession::api_can_attack(void* ctx, const char* id) {
        auto* s = static_cast<GameSession*>(ctx);
        if (!id || s->self_.rip) {
            return 0;
        }
        auto* t = s->find(id);
        if (!t || t->rip) {
            return 0;
        }
        if (s->now_ms() < s->self_.next_attack_ms) {
            return 0;
        }
        double range = s->self_.range > 1 ? s->self_.range : 40;
        // Official is_in_range: distance <= range + target.speed + 15
        return simple_distance(s->self_.x, s->self_.y, t->x, t->y) <= range + t->speed + 15;
    }

    int GameSession::api_can_use(void* ctx, const char* skill) {
        auto* s = static_cast<GameSession*>(ctx);
        if (!skill) {
            return 0;
        }
        std::string k(skill);
        double now = s->now_ms();
        if (k == "hp" || k == "use_hp") {
            return now >= s->next_hp_ms_;
        }
        if (k == "mp" || k == "use_mp") {
            return now >= s->next_mp_ms_;
        }
        return 1;
    }

    int GameSession::api_can_move_to(void* ctx, double x, double y) {
        auto* s = static_cast<GameSession*>(ctx);
        s->ensure_geo();
        return can_move(s->geo_, s->self_.x, s->self_.y, x, y) ? 1 : 0;
    }

    double GameSession::api_dist(void* /*ctx*/, double x1, double y1, double x2, double y2) {
        return simple_distance(x1, y1, x2, y2);
    }

    int GameSession::api_is_moving(void* ctx) {
        auto* s = static_cast<GameSession*>(ctx);
        return s->self_.moving && simple_distance(s->self_.x, s->self_.y, s->self_.going_x, s->self_.going_y) > 2;
    }

    void GameSession::api_respawn(void* ctx) {
        auto* s = static_cast<GameSession*>(ctx);
        s->io_.emit("respawn");
    }

    void GameSession::api_use(void* ctx, const char* what) {
        auto* s = static_cast<GameSession*>(ctx);
        if (!what) {
            return;
        }
        std::string w(what);
        std::string kind;
        if (w == "hp" || w == "use_hp") {
            kind = "hp";
        }
        else if (w == "mp" || w == "use_mp") {
            kind = "mp";
        }
        if (!kind.empty()) {
            int slot = monitoring::find_pot_slot(s->items_, s->g_, kind);
            if (slot >= 0) {
                Json o = Json::object();
                o["num"] = slot;
                s->io_.emit("equip", o);
                EWE::Log::Debug("use %s equip slot=%d\n", kind.c_str(), slot);
            }
            else {
                Json o = Json::object();
                o["item"] = kind;
                s->io_.emit("use", o);
                EWE::Log::Debug("use %s regen (no pot in inventory)\n", kind.c_str());
            }
            if (kind == "hp") {
                s->next_hp_ms_ = s->now_ms() + 2000;
            }
            else {
                s->next_mp_ms_ = s->now_ms() + 2000;
            }
            return;
        }
        Json o = Json::object();
        o["item"] = w;
        s->io_.emit("use", o);
    }

    void GameSession::api_loot(void* ctx) {
        auto* s = static_cast<GameSession*>(ctx);
        for (auto& [id, _] : s->chests_) {
            Json o = Json::object();
            o["id"] = json_id_value(id);
            s->io_.emit("open_chest", o);
        }
    }

    void GameSession::api_change_target(void* ctx, const char* id) {
        auto* s = static_cast<GameSession*>(ctx);
        std::string next = id ? id : "";
        if (next == s->target_id_) {
            return;
        }
        s->target_id_ = next;
        Json o = Json::object();
        o["id"] = s->target_id_.empty() ? Json("") : json_id_value(s->target_id_);
        o["xid"] = "";
        s->io_.emit("target", o);
    }

    void GameSession::api_attack(void* ctx, const char* id) {
        auto* s = static_cast<GameSession*>(ctx);
        if (!id) {
            return;
        }
        if (s->target_id_ != id) {
            api_change_target(ctx, id);
        }
        Json o = Json::object();
        o["id"] = json_id_value(id);
        s->io_.emit("attack", o);
        EWE::Log::Debug("attack emit id=%s\n", id);
        double freq = s->self_.frequency;
        double delay = 800;
        if (freq > 20 && freq < 5000) {
            delay = freq;
        }
        else if (freq > 0.2 && freq <= 20) {
            delay = 1000.0 / freq;
        }
        if (delay < 200) {
            delay = 200;
        }
        if (delay > 2000) {
            delay = 2000;
        }
        s->self_.next_attack_ms = s->now_ms() + delay;
    }

    bool GameSession::safe_move_to(double x, double y, bool /*may_clamp*/) {
        ensure_geo();
        auto m = calculate_move(geo_, self_.x, self_.y, x, y);
        double tx = m.first, ty = m.second;
        bool box_ok = can_move(geo_, self_.x, self_.y, tx, ty);
        if (simple_distance(self_.x, self_.y, tx, ty) < 1 || !box_ok) {
            auto u = unstuck_move(geo_, self_.x, self_.y, x, y);
            if (!u) {
                EWE::Log::Debug("move none (%.1f,%.1f) want (%.1f,%.1f)\n", self_.x, self_.y, x, y);
                return false;
            }
            tx = u->first;
            ty = u->second;
            EWE::Log::Debug("move unstuck (%.1f,%.1f)->(%.1f,%.1f) want (%.1f,%.1f)\n", self_.x, self_.y, tx, ty, x, y);
        }
        Json o = Json::object();
        o["x"] = self_.x;
        o["y"] = self_.y;
        o["going_x"] = tx;
        o["going_y"] = ty;
        o["m"] = map_m_;
        io_.emit("move", o);
        EWE::Log::Debug(
            "move emit (%.1f,%.1f)->(%.1f,%.1f) want (%.1f,%.1f) m=%d\n",
            self_.x,
            self_.y,
            tx,
            ty,
            x,
            y,
            map_m_
        );
        self_.going_x = tx;
        self_.going_y = ty;
        if (!box_ok) {
            self_.x = tx;
            self_.y = ty;
            self_.moving = false;
        }
        else {
            self_.moving = true;
        }
        return true;
    }

    void GameSession::api_move(void* ctx, double x, double y) {
        auto* s = static_cast<GameSession*>(ctx);
        s->clear_smart();
        s->safe_move_to(x, y, true);
    }

    void GameSession::ensure_geo() {
        if (map_.empty()) {
            return;
        }
        if (geo_.ok && geo_.name == map_) {
            return;
        }
        geo_ = map_geo_from_g(g_, map_);
        if (!geo_.ok) {
            try {
                std::string base = cfg_.base_url;
                while (base.ends_with('/')) {
                    base.pop_back();
                }
                auto r = http_get(base + "/maps/" + map_ + ".json");
                if (r.status >= 200 && r.status < 300 && !r.body.empty()) {
                    geo_ = map_geo_from_json(Json::parse(r.body), map_);
                }
            } catch (const std::exception& e) {
                log().debug(std::string("map json: ") + e.what());
            }
        }
        if (geo_.ok) {
            log().info(
                "loaded map geometry " + map_ + " x_lines=" + std::to_string(geo_.x_lines.size()) +
                " y_lines=" + std::to_string(geo_.y_lines.size())
            );
        }
        else {
            log().warn("no wall data for map " + map_ + "; refusing long unclamped moves");
        }
    }

    void GameSession::clear_smart() {
        smart_ = {};
    }

    void GameSession::stop_in_place() {
        self_.going_x = self_.x;
        self_.going_y = self_.y;
        self_.moving = false;
        Json o = Json::object();
        o["x"] = self_.x;
        o["y"] = self_.y;
        o["going_x"] = self_.x;
        o["going_y"] = self_.y;
        o["m"] = map_m_;
        io_.emit("move", o);
        EWE::Log::Debug("stop in place (%.1f,%.1f)\n", self_.x, self_.y);
    }

    bool GameSession::resolve_named_dest(const char* dest, double& x, double& y) {
        if (!dest) {
            return false;
        }
        if (auto* n = api_nearest(this, dest)) {
            x = n->real_x;
            y = n->real_y;
            return true;
        }
        if (g_.is_object() && g_["maps"].is_object() && g_["maps"].contains(map_)) {
            const auto& m = g_["maps"][map_];
            if (m["monsters"].is_array()) {
                for (const auto& sp : m["monsters"].as_array()) {
                    if (sp["type"].as_string_opt() != std::optional<std::string>{dest}) {
                        continue;
                    }
                    if (sp["boundary"].is_array() && sp["boundary"].as_array().size() >= 4) {
                        auto& b = sp["boundary"].as_array();
                        x = (b[0].as_number() + b[2].as_number()) / 2;
                        y = (b[1].as_number() + b[3].as_number()) / 2;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void GameSession::start_smart(double x, double y) {
        ensure_geo();
        double now = now_ms();
        if (smart_.active && simple_distance(x, y, smart_.dest_x, smart_.dest_y) < 80) {
            EWE::Log::Debug(
                "smart keep dest (%.1f,%.1f)->(%.1f,%.1f) plot=%zu i=%zu\n",
                self_.x,
                self_.y,
                x,
                y,
                smart_.plot.size(),
                smart_.i
            );
            smart_.dest_x = x;
            smart_.dest_y = y;
            if (!smart_.plot.empty() && can_move(geo_, self_.x, self_.y, x, y)) {
                smart_.plot = {{x, y}};
                smart_.i = 0;
            }
            return;
        }
        if (now < smart_.fail_until_ms && simple_distance(x, y, smart_.dest_x, smart_.dest_y) < 80) {
            EWE::Log::Debug("smart cooldown no-path to (%.1f,%.1f)\n", x, y);
            return;
        }
        smart_.dest_x = x;
        smart_.dest_y = y;
        smart_.plot = find_path(geo_, self_.x, self_.y, x, y);
        smart_.i = 0;
        smart_.active = !smart_.plot.empty();
        if (!smart_.active) {
            smart_.fail_until_ms = now + 1000;
            EWE::Log::Warning(
                "smart no path (%.1f,%.1f)->(%.1f,%.1f) geo=%d lines=%zu\n",
                self_.x,
                self_.y,
                x,
                y,
                geo_.ok ? 1 : 0,
                geo_.x_lines.size() + geo_.y_lines.size()
            );
            stop_in_place();
            return;
        }
        smart_.fail_until_ms = 0;
        bool stepped = false;
        for (; smart_.i < smart_.plot.size(); ++smart_.i) {
            EWE::Log::Debug(
                "smart path n=%zu i=%zu (%.1f,%.1f)->(%.1f,%.1f) wp=(%.1f,%.1f)\n",
                smart_.plot.size(),
                smart_.i,
                self_.x,
                self_.y,
                x,
                y,
                smart_.plot[smart_.i].first,
                smart_.plot[smart_.i].second
            );
            if (safe_move_to(smart_.plot[smart_.i].first, smart_.plot[smart_.i].second, false)) {
                stepped = true;
                break;
            }
        }
        if (!stepped) {
            EWE::Log::Warning("smart first step refused, stopping\n");
            smart_.active = false;
            stop_in_place();
        }
    }

    void GameSession::continue_smart() {
        if (!smart_.active) {
            return;
        }
        ensure_geo();
        if (self_.moving && simple_distance(self_.x, self_.y, self_.going_x, self_.going_y) > 2) {
            return;
        }
        if (simple_distance(self_.x, self_.y, smart_.dest_x, smart_.dest_y) < 12) {
            EWE::Log::Debug("smart arrived (%.1f,%.1f)\n", self_.x, self_.y);
            clear_smart();
            return;
        }
        if (can_move(geo_, self_.x, self_.y, smart_.dest_x, smart_.dest_y)) {
            safe_move_to(smart_.dest_x, smart_.dest_y, false);
            return;
        }
        while (smart_.i < smart_.plot.size() &&
               simple_distance(self_.x, self_.y, smart_.plot[smart_.i].first, smart_.plot[smart_.i].second) < 16) {
            ++smart_.i;
        }
        if (smart_.i >= smart_.plot.size()) {
            smart_.plot = find_path(geo_, self_.x, self_.y, smart_.dest_x, smart_.dest_y);
            smart_.i = 0;
            if (smart_.plot.empty()) {
                EWE::Log::Warning(
                    "smart replan failed at (%.1f,%.1f) dest=(%.1f,%.1f)\n",
                    self_.x,
                    self_.y,
                    smart_.dest_x,
                    smart_.dest_y
                );
                smart_.active = false;
                smart_.fail_until_ms = now_ms() + 1000;
                stop_in_place();
                return;
            }
        }
        auto [wx, wy] = smart_.plot[smart_.i];
        if (!can_move(geo_, self_.x, self_.y, wx, wy)) {
            EWE::Log::Debug("smart waypoint blocked (%.1f,%.1f)->(%.1f,%.1f) replan\n", self_.x, self_.y, wx, wy);
            smart_.plot = find_path(geo_, self_.x, self_.y, smart_.dest_x, smart_.dest_y);
            smart_.i = 0;
            if (smart_.plot.empty()) {
                smart_.active = false;
                smart_.fail_until_ms = now_ms() + 1000;
                stop_in_place();
                return;
            }
            wx = smart_.plot[0].first;
            wy = smart_.plot[0].second;
        }
        if (!self_.moving || simple_distance(self_.going_x, self_.going_y, wx, wy) > 4) {
            EWE::Log::Debug("smart follow i=%zu/%zu wp=(%.1f,%.1f)\n", smart_.i, smart_.plot.size(), wx, wy);
            safe_move_to(wx, wy, false);
        }
    }

    void GameSession::api_smart_xy(void* ctx, double x, double y) {
        static_cast<GameSession*>(ctx)->start_smart(x, y);
    }

    void GameSession::api_smart_named(void* ctx, const char* dest) {
        auto* s = static_cast<GameSession*>(ctx);
        double x = 0, y = 0;
        if (!s->resolve_named_dest(dest, x, y)) {
            EWE::Log::Warning("smart_move no dest for %s\n", dest ? dest : "");
            return;
        }
        s->start_smart(x, y);
    }

    void GameSession::api_log(void* /*ctx*/, const char* msg) {
        log().info(msg ? msg : "");
    }

    void GameSession::fill_view(CaracalEntity& out, const Entity& e) {
        out.rip = e.rip ? 1 : 0;
        out.hp = e.hp;
        out.max_hp = e.max_hp;
        out.mp = e.mp;
        out.max_mp = e.max_mp;
        out.x = e.x;
        out.y = e.y;
        out.real_x = e.x;
        out.real_y = e.y;
        out.range = e.range;
        out.moving = e.moving ? 1 : 0;
        out.going_x = e.going_x;
        out.going_y = e.going_y;
    }

    Entity* GameSession::find(const std::string& id) {
        if (id == self_.id) {
            return &self_;
        }
        auto it = monsters_.find(id);
        return it == monsters_.end() ? nullptr : &it->second;
    }
    const Entity* GameSession::find(const std::string& id) const {
        if (id == self_.id) {
            return &self_;
        }
        auto it = monsters_.find(id);
        return it == monsters_.end() ? nullptr : &it->second;
    }

    double GameSession::now_ms() const {
        return clock_ms();
    }

    std::string GameSession::ws_url() const {
        auto host = parse_realm_host(cfg_.realm_address);
        int port = 443;
        if (cfg_.realm_port) {
            port = *cfg_.realm_port;
        }
        else if (host.port) {
            port = *host.port;
        }
        else if (cfg_.base_url.starts_with("http://")) {
            port = 80;
        }
        bool tls = !cfg_.base_url.starts_with("http://");
        std::string h = host.host.empty() ? cfg_.realm_address : host.host;
        std::string path = cfg_.realm_path.empty() ? "/socket.io/" : cfg_.realm_path;
        if (path.back() != '/') {
            path += '/';
        }
        path += "?EIO=4&transport=websocket";
        std::string scheme = tls ? "wss://" : "ws://";
        return scheme + h + ":" + std::to_string(port) + path;
    }

    void GameSession::emit_loaded_and_auth() {
        Json loaded = Json::object();
        loaded["success"] = 1;
        loaded["width"] = 1920;
        loaded["height"] = 1080;
        loaded["scale"] = 2;
        loaded["no_html"] = 1;
        loaded["no_graphics"] = 1;
        io_.emit("loaded", loaded);

        auto dash = cfg_.session.find('-');
        std::string uid = dash == std::string::npos ? cfg_.session : cfg_.session.substr(0, dash);
        std::string auth = dash == std::string::npos ? "" : cfg_.session.substr(dash + 1);
        Json a = Json::object();
        a["user"] = uid;
        a["auth"] = auth;
        a["character"] = cfg_.cid;
        a["width"] = 1920;
        a["height"] = 1080;
        a["scale"] = 2;
        a["no_html"] = 1;
        a["no_graphics"] = 1;
        io_.emit("auth", a);
        auth_emitted_ = true;
    }

    void GameSession::note_map_index(const Json& data) {
        if (data.is_object() && data["m"].is_number()) {
            map_m_ = static_cast<int>(data["m"].as_number());
        }
    }

    void GameSession::simulate_move(double now_ms) {
        if (last_sim_ms_ == 0) {
            last_sim_ms_ = now_ms;
        }
        double dt = (now_ms - last_sim_ms_) / 1000.0;
        last_sim_ms_ = now_ms;
        if (!self_.moving || dt <= 0) {
            return;
        }
        double dx = self_.going_x - self_.x;
        double dy = self_.going_y - self_.y;
        double dist = std::hypot(dx, dy);
        double step = std::max(self_.speed, 30.0) * dt;
        if (dist <= step || dist < 1) {
            self_.x = self_.going_x;
            self_.y = self_.going_y;
            self_.moving = false;
            return;
        }
        self_.x += dx / dist * step;
        self_.y += dy / dist * step;
    }

    void GameSession::apply_self(const Json& data) {
        auto next_atk = self_.next_attack_ms;
        bool commanded = self_.moving;
        double gx = self_.going_x;
        double gy = self_.going_y;
        double lx = self_.x, ly = self_.y;
        self_ = entity_from_json(data, self_.id);
        self_.next_attack_ms = next_atk;
        note_map_index(data);
        if (data["map"].is_string()) {
            map_ = data["map"].as_string();
        }
        if (data["in"].is_string() && map_.empty()) {
            map_ = data["in"].as_string();
        }
        ensure_geo();
        if (commanded) {
            self_.going_x = gx;
            self_.going_y = gy;
            if (simple_distance(lx, ly, gx, gy) + 1 < simple_distance(self_.x, self_.y, gx, gy) &&
                simple_distance(self_.x, self_.y, lx, ly) < 24) {
                self_.x = lx;
                self_.y = ly;
            }
            self_.moving = simple_distance(self_.x, self_.y, gx, gy) > 2;
        }
        if (self_.range <= 1) {
            self_.range = 40;
        }
        if (data["items"].is_array()) {
            items_ = data["items"];
        }
    }

    void GameSession::merge_entities(const Json& data, bool reset) {
        if (reset) {
            monsters_.clear();
        }
        ingest_map(monsters_, data["monsters"]);
        if (data["players"].is_array() || data["players"].is_object()) {
            // keep our hp/position in sync if present
            if (data["players"].is_object() && data["players"].contains(self_.id)) {
                apply_self(data["players"][self_.id]);
            }
            else if (data["players"].is_array()) {
                for (const auto& p : data["players"].as_array()) {
                    if (json_id_string(p["id"]) == self_.id) {
                        apply_self(p);
                    }
                }
            }
        }
    }

    void GameSession::handle_event(const SocketIoPacket& p) {
        if (character::is_auth_reject_payload(p.data) || character::is_auth_reject_text(p.event)) {
            auth_fail_ = true;
            if (on_auth_fail) {
                on_auth_fail(p.data.is_string() ? p.data.as_string() : p.event);
            }
            return;
        }
        if (p.event == "welcome") {
            if (!auth_emitted_) {
                emit_loaded_and_auth();
            }
            return;
        }
        if (p.event == "start" || p.event == "player") {
            apply_self(p.data);
            if (!in_game_) {
                in_game_ = true;
                last_sim_ms_ = now_ms();
                if (on_connected) {
                    on_connected();
                }
            }
            return;
        }
        if (p.event == "entities") {
            bool reset = p.data["type"].as_string_opt() == std::optional<std::string>{"all"};
            merge_entities(p.data, reset);
            if (p.data["map"].is_string()) {
                map_ = p.data["map"].as_string();
            }
            else if (p.data["in"].is_string()) {
                map_ = p.data["in"].as_string();
            }
            note_map_index(p.data);
            ensure_geo();
            return;
        }
        if (p.event == "drop" || p.event == "chest_opened") {
            if (p.data.is_object()) {
                auto id = json_id_string(p.data["id"]);
                if (!id.empty()) {
                    if (p.event == "drop") {
                        chests_[id] = p.data;
                    }
                    else {
                        chests_.erase(id);
                    }
                }
            }
            return;
        }
        if (p.event == "death") {
            auto id = p.data.is_object() ? json_id_string(p.data["id"]) : std::string{};
            if (!id.empty()) {
                if (id == self_.id) {
                    self_.rip = true;
                }
                monsters_.erase(id);
            }
            return;
        }
        if (p.event == "disappearing" || p.event == "notthere") {
            auto id = p.data.is_object() ? json_id_string(p.data["id"]) : std::string{};
            if (!id.empty()) {
                monsters_.erase(id);
            }
            return;
        }
        if (p.event == "hit" && p.data.is_object()) {
            auto id = json_id_string(p.data["id"]);
            if (!id.empty()) {
                if (p.data["kill"].truthy()) {
                    if (id == self_.id) {
                        self_.rip = true;
                    }
                    monsters_.erase(id);
                }
                else if (auto* m = find(id)) {
                    if (p.data["hp"].is_number()) {
                        m->hp = p.data["hp"].as_number();
                    }
                    if (m->hp <= 0) {
                        monsters_.erase(id);
                    }
                }
            }
            return;
        }
        if (p.event == "new_map") {
            monsters_.clear();
            if (p.data.is_object()) {
                if (p.data["name"].is_string()) {
                    map_ = p.data["name"].as_string();
                }
                if (p.data["x"].is_number()) {
                    self_.x = p.data["x"].as_number();
                }
                if (p.data["y"].is_number()) {
                    self_.y = p.data["y"].as_number();
                }
                note_map_index(p.data);
                self_.moving = false;
                self_.going_x = self_.x;
                self_.going_y = self_.y;
                clear_smart();
                ensure_geo();
            }
            return;
        }
        if (p.event == "game_log" || p.event == "game_error" || p.event == "game_response") {
            EWE::Log::Debug("%s %s\n", p.event.c_str(), p.data.dump().c_str());
            if (character::is_auth_reject_payload(p.data)) {
                auth_fail_ = true;
                if (on_auth_fail) {
                    on_auth_fail("game_log");
                }
            }
            return;
        }
        if (p.event == "disconnect" || p.event == "disconnect_reason") {
            running_ = false;
            if (on_disconnect) {
                on_disconnect();
            }
        }
    }

    void GameSession::start(const CharacterConnect& cfg, const Json& g) {
        cfg_ = cfg;
        g_ = g;
        running_ = true;
        auth_emitted_ = false;
        in_game_ = false;
        auto url = ws_url();
        log().info("connecting " + url);
        io_.connect(url);
        emit_loaded_and_auth();

        CaracalApi api{};
        api.abi = CARACAL_PLUGIN_ABI;
        api.ctx = this;
        api.self = api_self;
        api.nearest_monster = api_nearest;
        api.can_attack = api_can_attack;
        api.can_use = api_can_use;
        api.can_move_to = api_can_move_to;
        api.simple_distance = api_dist;
        api.is_moving = api_is_moving;
        api.respawn = api_respawn;
        api.use = api_use;
        api.loot = api_loot;
        api.change_target = api_change_target;
        api.attack = api_attack;
        api.move = api_move;
        api.smart_move_xy = api_smart_xy;
        api.smart_move_named = api_smart_named;
        api.log = api_log;
        plugin_.bind(api);
        plugin_.load(cfg_.plugin_path);
    }

    void GameSession::stop() {
        running_ = false;
        try {
            plugin_.unload();
        } catch (...) {}
        try {
            io_.disconnect();
        } catch (...) {}
        io_.close();
    }

    void GameSession::run_forever(std::atomic<bool>* stop_a, std::atomic<bool>* stop_b) {
        auto next_reload = clock_ms() + 1000;
        while (running_ && io_.alive() && !auth_fail_) {
            if (stop_a && stop_a->load()) {
                break;
            }
            if (stop_b && !stop_b->load()) {
                break;
            }
            try {
                auto pkt = io_.poll(50);
                if (pkt) {
                    handle_event(*pkt);
                }
            } catch (const std::exception& e) {
                log().error(std::string("session: ") + e.what());
                break;
            }
            double now = clock_ms();
            if (in_game_) {
                simulate_move(now);
                continue_smart();
                plugin_.on_tick(now);
            }
            if (now >= next_reload) {
                try {
                    plugin_.reload_if_changed();
                } catch (const std::exception& e) {
                    log().warn(std::string("plugin reload failed: ") + e.what());
                }
                next_reload = now + 1000;
            }
        }
        if (on_disconnect) {
            on_disconnect();
        }
    }

} // namespace caracal
