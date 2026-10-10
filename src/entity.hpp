#pragma once

#include "json.hpp"

#include <cmath>
#include <string>

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

inline Entity entity_from_json(const Json& j, std::string fallback_id = {}) {
  Entity e;
  e.id = j["id"].as_string_opt().value_or(fallback_id);
  e.type = j["type"].as_string_opt().value_or(j["mtype"].as_string_opt().value_or(""));
  e.name = j["name"].as_string_opt().value_or(e.id);
  e.rip = j["rip"].truthy();
  e.hp = j["hp"].as_number_opt().value_or(0);
  e.max_hp = j["max_hp"].as_number_opt().value_or(1);
  e.mp = j["mp"].as_number_opt().value_or(0);
  e.max_mp = j["max_mp"].as_number_opt().value_or(1);
  e.x = j["x"].as_number_opt().value_or(j["real_x"].as_number_opt().value_or(0));
  e.y = j["y"].as_number_opt().value_or(j["real_y"].as_number_opt().value_or(0));
  e.going_x = j["going_x"].as_number_opt().value_or(e.x);
  e.going_y = j["going_y"].as_number_opt().value_or(e.y);
  e.range = j["range"].as_number_opt().value_or(40);
  e.speed = j["speed"].as_number_opt().value_or(30);
  e.frequency = j["frequency"].as_number_opt().value_or(1);
  e.moving = j["moving"].truthy() || simple_distance(e.x, e.y, e.going_x, e.going_y) > 2;
  return e;
}

}  // namespace caracal
