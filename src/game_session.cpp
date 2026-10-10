#include "game_session.hpp"

#include "character_thread.hpp"
#include "log_utils.hpp"
#include "realm_rtt.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace caracal {
namespace {

double clock_ms() {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count());
}

void ingest_map(std::unordered_map<std::string, Entity>& dest, const Json& node) {
  if (node.is_array()) {
    for (const auto& e : node.as_array()) {
      auto ent = entity_from_json(e);
      if (!ent.id.empty()) dest[ent.id] = std::move(ent);
    }
  } else if (node.is_object()) {
    for (const auto& [id, e] : node.as_object()) {
      dest[id] = entity_from_json(e, id);
    }
  }
}

}  // namespace

const CaracalEntity* GameSession::api_self(void* ctx) {
  auto* s = static_cast<GameSession*>(ctx);
  s->fill_view(s->self_view_, s->self_);
  s->self_id_store_ = s->self_.id;
  s->self_type_store_ = s->self_.type;
  s->self_name_store_ = s->self_.name;
  s->self_view_.id = s->self_id_store_.c_str();
  s->self_view_.type = s->self_type_store_.c_str();
  s->self_view_.name = s->self_name_store_.c_str();
  return &s->self_view_;
}

const CaracalEntity* GameSession::api_nearest(void* ctx, const char* type) {
  auto* s = static_cast<GameSession*>(ctx);
  const Entity* best = nullptr;
  double best_d = 1e100;
  std::string want = type ? type : "";
  for (auto& [_, m] : s->monsters_) {
    if (!want.empty() && m.type != want) continue;
    double d = simple_distance(s->self_.x, s->self_.y, m.x, m.y);
    if (d < best_d) {
      best_d = d;
      best = &m;
    }
  }
  if (!best) return nullptr;
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
  if (!id || s->self_.rip) return 0;
  auto* t = s->find(id);
  if (!t) return 0;
  if (s->now_ms() < s->self_.next_attack_ms) return 0;
  return simple_distance(s->self_.x, s->self_.y, t->x, t->y) <= s->self_.range + 1;
}

int GameSession::api_can_use(void* ctx, const char* skill) {
  auto* s = static_cast<GameSession*>(ctx);
  if (!skill) return 0;
  std::string k(skill);
  double now = s->now_ms();
  if (k == "hp" || k == "use_hp") return now >= s->next_hp_ms_;
  if (k == "mp" || k == "use_mp") return now >= s->next_mp_ms_;
  return 1;
}

int GameSession::api_can_move_to(void* /*ctx*/, double /*x*/, double /*y*/) { return 1; }

double GameSession::api_dist(void* /*ctx*/, double x1, double y1, double x2, double y2) {
  return simple_distance(x1, y1, x2, y2);
}

int GameSession::api_is_moving(void* ctx) {
  auto* s = static_cast<GameSession*>(ctx);
  return s->self_.moving ||
         simple_distance(s->self_.x, s->self_.y, s->self_.going_x, s->self_.going_y) > 2;
}

void GameSession::api_respawn(void* ctx) {
  auto* s = static_cast<GameSession*>(ctx);
  s->io_.emit("respawn");
}

void GameSession::api_use(void* ctx, const char* what) {
  auto* s = static_cast<GameSession*>(ctx);
  if (!what) return;
  std::string w(what);
  if (w == "hp" || w == "use_hp") {
    s->io_.emit("use_hp");
    s->next_hp_ms_ = s->now_ms() + 2000;
  } else if (w == "mp" || w == "use_mp") {
    s->io_.emit("use_mp");
    s->next_mp_ms_ = s->now_ms() + 2000;
  } else {
    Json o = Json::object();
    o["item"] = w;
    s->io_.emit("use", o);
  }
}

void GameSession::api_loot(void* ctx) {
  auto* s = static_cast<GameSession*>(ctx);
  for (auto& [id, _] : s->chests_) {
    Json o = Json::object();
    o["id"] = id;
    s->io_.emit("open_chest", o);
  }
}

void GameSession::api_change_target(void* ctx, const char* id) {
  auto* s = static_cast<GameSession*>(ctx);
  s->target_id_ = id ? id : "";
  Json o = Json::object();
  o["id"] = s->target_id_;
  s->io_.emit("target", o);
}

void GameSession::api_attack(void* ctx, const char* id) {
  auto* s = static_cast<GameSession*>(ctx);
  if (!id) return;
  Json o = Json::object();
  o["id"] = id;
  s->io_.emit("attack", o);
  double delay = s->self_.frequency > 0 ? 1000.0 / s->self_.frequency : 800.0;
  s->self_.next_attack_ms = s->now_ms() + delay;
}

void GameSession::api_move(void* ctx, double x, double y) {
  auto* s = static_cast<GameSession*>(ctx);
  Json o = Json::object();
  o["x"] = s->self_.x;
  o["y"] = s->self_.y;
  o["going_x"] = x;
  o["going_y"] = y;
  s->io_.emit("move", o);
  s->self_.going_x = x;
  s->self_.going_y = y;
  s->self_.moving = true;
}

void GameSession::api_smart_xy(void* ctx, double x, double y) { api_move(ctx, x, y); }

void GameSession::api_smart_named(void* ctx, const char* dest) {
  auto* s = static_cast<GameSession*>(ctx);
  if (!dest) return;
  if (auto* n = api_nearest(ctx, dest)) {
    api_move(ctx, n->real_x, n->real_y);
    return;
  }
  if (s->g_.is_object() && s->g_["maps"].is_object() && s->g_["maps"].contains(s->map_)) {
    const auto& m = s->g_["maps"][s->map_];
    if (m["monsters"].is_array()) {
      for (const auto& sp : m["monsters"].as_array()) {
        if (sp["type"].as_string_opt() != std::optional<std::string>{dest}) continue;
        if (sp["boundary"].is_array() && sp["boundary"].as_array().size() >= 4) {
          auto& b = sp["boundary"].as_array();
          double x = (b[0].as_number() + b[2].as_number()) / 2;
          double y = (b[1].as_number() + b[3].as_number()) / 2;
          api_move(ctx, x, y);
          return;
        }
      }
    }
  }
  log().debug(std::string("smart_move no dest for ") + dest);
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
}

Entity* GameSession::find(const std::string& id) {
  if (id == self_.id) return &self_;
  auto it = monsters_.find(id);
  return it == monsters_.end() ? nullptr : &it->second;
}
const Entity* GameSession::find(const std::string& id) const {
  if (id == self_.id) return &self_;
  auto it = monsters_.find(id);
  return it == monsters_.end() ? nullptr : &it->second;
}

double GameSession::now_ms() const { return clock_ms(); }

std::string GameSession::ws_url() const {
  auto host = parse_realm_host(cfg_.realm_address);
  int port = 443;
  if (cfg_.realm_port) port = *cfg_.realm_port;
  else if (host.port) port = *host.port;
  else if (cfg_.base_url.starts_with("http://")) port = 80;
  bool tls = !cfg_.base_url.starts_with("http://");
  std::string h = host.host.empty() ? cfg_.realm_address : host.host;
  std::string path = cfg_.realm_path.empty() ? "/socket.io/" : cfg_.realm_path;
  if (path.back() != '/') path += '/';
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

void GameSession::apply_self(const Json& data) {
  self_ = entity_from_json(data, self_.id);
  if (data["map"].is_string()) map_ = data["map"].as_string();
  if (data["in"].is_string() && map_.empty()) map_ = data["in"].as_string();
}

void GameSession::merge_entities(const Json& data, bool reset) {
  if (reset) monsters_.clear();
  ingest_map(monsters_, data["monsters"]);
  if (data["players"].is_array() || data["players"].is_object()) {
    // keep our hp/position in sync if present
    if (data["players"].is_object() && data["players"].contains(self_.id)) {
      apply_self(data["players"][self_.id]);
    } else if (data["players"].is_array()) {
      for (const auto& p : data["players"].as_array()) {
        if (p["id"].as_string_opt() == std::optional<std::string>{self_.id}) apply_self(p);
      }
    }
  }
}

void GameSession::handle_event(const SocketIoPacket& p) {
  if (character::is_auth_reject_payload(p.data) || character::is_auth_reject_text(p.event)) {
    auth_fail_ = true;
    if (on_auth_fail) on_auth_fail(p.data.is_string() ? p.data.as_string() : p.event);
    return;
  }
  if (p.event == "welcome") {
    if (!auth_emitted_) emit_loaded_and_auth();
    return;
  }
  if (p.event == "start" || p.event == "player") {
    apply_self(p.data);
    if (!in_game_) {
      in_game_ = true;
      if (on_connected) on_connected();
    }
    return;
  }
  if (p.event == "entities") {
    bool reset = p.data["type"].as_string_opt() == std::optional<std::string>{"all"};
    merge_entities(p.data, reset);
    if (p.data["in"].is_string()) map_ = p.data["in"].as_string();
    return;
  }
  if (p.event == "drop" || p.event == "chest_opened") {
    if (p.data.is_object() && p.data["id"].is_string()) {
      if (p.event == "drop") chests_[p.data["id"].as_string()] = p.data;
      else chests_.erase(p.data["id"].as_string());
    }
    return;
  }
  if (p.event == "death") {
    auto id = p.data.is_object() ? p.data["id"].as_string_opt() : std::nullopt;
    if (id) {
      if (*id == self_.id) self_.rip = true;
      monsters_.erase(*id);
    }
    return;
  }
  if (p.event == "disappearing" || p.event == "notthere") {
    auto id = p.data.is_object() ? p.data["id"].as_string_opt() : std::nullopt;
    if (id) monsters_.erase(*id);
    return;
  }
  if (p.event == "new_map") {
    monsters_.clear();
    if (p.data.is_object()) {
      if (p.data["name"].is_string()) map_ = p.data["name"].as_string();
      if (p.data["x"].is_number()) self_.x = p.data["x"].as_number();
      if (p.data["y"].is_number()) self_.y = p.data["y"].as_number();
    }
    return;
  }
  if (p.event == "game_log" || p.event == "game_error" || p.event == "game_response") {
    if (character::is_auth_reject_payload(p.data)) {
      auth_fail_ = true;
      if (on_auth_fail) on_auth_fail("game_log");
    }
    return;
  }
  if (p.event == "disconnect" || p.event == "disconnect_reason") {
    running_ = false;
    if (on_disconnect) on_disconnect();
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
  } catch (...) {
  }
  io_.close();
}

void GameSession::run_forever() {
  auto next_reload = clock_ms() + 1000;
  while (running_ && io_.alive() && !auth_fail_) {
    try {
      auto pkt = io_.poll(50);
      if (pkt) handle_event(*pkt);
    } catch (const std::exception& e) {
      log().error(std::string("session: ") + e.what());
      break;
    }
    double now = clock_ms();
    if (in_game_) plugin_.on_tick(now);
    if (now >= next_reload) {
      try {
        plugin_.reload_if_changed();
      } catch (const std::exception& e) {
        log().warn(std::string("plugin reload failed: ") + e.what());
      }
      next_reload = now + 1000;
    }
  }
  if (on_disconnect) on_disconnect();
}

}  // namespace caracal
