#pragma once

#include "json.hpp"

#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

namespace caracal {

struct Entity {
  std::string id;
  std::string type;
  std::string name;
  bool rip = false;
  double hp = 0;
  double max_hp = 1;
  double mp = 0;
  double max_mp = 1;
  double x = 0;
  double y = 0;
  double going_x = 0;
  double going_y = 0;
  double range = 40;
  double speed = 30;
  double frequency = 1;
  double next_attack_ms = 0;
  bool moving = false;
};

inline double simple_distance(double x1, double y1, double x2, double y2) {
  return std::hypot(x2 - x1, y2 - y1);
}

inline std::string json_id_string(const Json& j, std::string fallback = {}) {
  if (j.is_string()) return j.as_string();
  if (j.is_number()) {
    double n = j.as_number();
    if (std::isfinite(n) && n == std::floor(n)) {
      return std::to_string(static_cast<long long>(n));
    }
    return std::to_string(n);
  }
  return fallback;
}

inline Json json_id_value(const std::string& id) {
  if (id.empty()) return Json(nullptr);
  try {
    std::size_t idx = 0;
    long long n = std::stoll(id, &idx);
    if (idx == id.size()) return Json(n);
  } catch (...) {
  }
  return Json(id);
}

inline bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

inline bool entity_matches_type(const Entity& e, std::string_view want) {
  if (want.empty()) return true;
  return iequals(e.type, want) || iequals(e.name, want);
}

inline Entity entity_from_json(const Json& j, std::string fallback_id = {}) {
  Entity e;
  e.id = json_id_string(j["id"], std::move(fallback_id));
  auto mtype = j["mtype"].as_string_opt();
  auto type = j["type"].as_string_opt();
  if (mtype && !mtype->empty()) e.type = *mtype;
  else if (type && *type != "monster" && *type != "character") e.type = *type;
  else e.type = mtype.value_or(type.value_or(""));
  e.name = j["name"].as_string_opt().value_or(e.type.empty() ? e.id : e.type);
  e.rip = j["rip"].truthy();
  e.hp = j["hp"].as_number_opt().value_or(0);
  e.max_hp = j["max_hp"].as_number_opt().value_or(1);
  e.mp = j["mp"].as_number_opt().value_or(0);
  e.max_mp = j["max_mp"].as_number_opt().value_or(1);
  e.x = j["x"].as_number_opt().value_or(j["real_x"].as_number_opt().value_or(0));
  e.y = j["y"].as_number_opt().value_or(j["real_y"].as_number_opt().value_or(0));
  e.going_x = j.contains("going_x") && j["going_x"].is_number() ? j["going_x"].as_number() : e.x;
  e.going_y = j.contains("going_y") && j["going_y"].is_number() ? j["going_y"].as_number() : e.y;
  e.range = j["range"].as_number_opt().value_or(0);
  if (e.range <= 0) e.range = 40;
  e.speed = j["speed"].as_number_opt().value_or(30);
  e.frequency = j["frequency"].as_number_opt().value_or(1);
  e.moving = j["moving"].truthy();
  return e;
}

}  // namespace caracal
