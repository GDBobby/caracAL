#pragma once

#include "entity.hpp"
#include "json.hpp"
#include "pathing.hpp"
#include "plugin_abi.hpp"
#include "plugin_host.hpp"
#include "socket_io.hpp"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace caracal {

struct CharacterConnect {
  std::string base_url;
  std::string session;
  std::string cid;
  std::string cname;
  std::string realm_address;
  std::string realm_path = "/socket.io/";
  std::optional<int> realm_port;
  std::string plugin_path;
};

class GameSession {
 public:
  std::function<void(std::string)> on_auth_fail;
  std::function<void()> on_connected;
  std::function<void()> on_disconnect;

  void start(const CharacterConnect& cfg, const Json& g = Json(nullptr));
  void stop();
  void run_forever(std::atomic<bool>* stop_a = nullptr, std::atomic<bool>* stop_b = nullptr);

  bool in_game() const { return in_game_; }

 private:
  static const CaracalEntity* api_self(void* ctx);
  static const CaracalEntity* api_nearest(void* ctx, const char* type);
  static int api_can_attack(void* ctx, const char* id);
  static int api_can_use(void* ctx, const char* skill);
  static int api_can_move_to(void* ctx, double x, double y);
  static double api_dist(void* ctx, double x1, double y1, double x2, double y2);
  static int api_is_moving(void* ctx);
  static void api_respawn(void* ctx);
  static void api_use(void* ctx, const char* what);
  static void api_loot(void* ctx);
  static void api_change_target(void* ctx, const char* id);
  static void api_attack(void* ctx, const char* id);
  static void api_move(void* ctx, double x, double y);
  static void api_smart_xy(void* ctx, double x, double y);
  static void api_smart_named(void* ctx, const char* dest);
  static void api_log(void* ctx, const char* msg);

  void handle_event(const SocketIoPacket& p);
  void merge_entities(const Json& data, bool reset);
  void apply_self(const Json& data);
  void emit_loaded_and_auth();
  void fill_view(CaracalEntity& out, const Entity& e);
  void simulate_move(double now_ms);
  void note_map_index(const Json& data);
  void ensure_geo();
  bool safe_move_to(double x, double y, bool may_clamp = false);
  void stop_in_place();
  bool resolve_named_dest(const char* dest, double& x, double& y);
  void start_smart(double x, double y);
  void continue_smart();
  void clear_smart();
  Entity* find(const std::string& id);
  const Entity* find(const std::string& id) const;
  double now_ms() const;
  std::string ws_url() const;

  CharacterConnect cfg_;
  Json g_ = Json(nullptr);
  SocketIo io_;
  PluginHost plugin_;
  Entity self_;
  Json items_ = Json::array();
  std::unordered_map<std::string, Entity> monsters_;
  std::unordered_map<std::string, Json> chests_;
  std::string target_id_;
  std::string map_;
  MapGeo geo_{};
  struct SmartState {
    bool active = false;
    double dest_x = 0;
    double dest_y = 0;
    std::vector<std::pair<double, double>> plot;
    std::size_t i = 0;
    double fail_until_ms = 0;
  } smart_{};
  int map_m_ = 0;
  double last_sim_ms_ = 0;
  bool in_game_ = false;
  bool running_ = false;
  bool auth_emitted_ = false;
  bool auth_fail_ = false;
  double next_hp_ms_ = 0;
  double next_mp_ms_ = 0;
  CaracalEntity self_view_{};
  CaracalEntity nearest_view_{};
  std::string self_id_store_;
  std::string self_type_store_;
  std::string self_name_store_;
  std::string nearest_id_store_;
  std::string nearest_type_store_;
  std::string nearest_name_store_;
};

}  // namespace caracal
