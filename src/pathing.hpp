#pragma once

#include "json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace caracal {

struct XLine {
  double x = 0;
  double y1 = 0;
  double y2 = 0;
};
struct YLine {
  double y = 0;
  double x1 = 0;
  double x2 = 0;
};

struct MapGeo {
  bool ok = false;
  std::string name;
  double min_x = 0;
  double max_x = 0;
  double min_y = 0;
  double max_y = 0;
  bool has_bounds = false;
  std::vector<XLine> x_lines;
  std::vector<YLine> y_lines;
};

/* Official character.base default: set_base() in common_functions.js */
inline constexpr double actor_h = 8;
inline constexpr double actor_v = 7;
inline constexpr double actor_vn = 2;
inline constexpr double map_actor_buffer = actor_h;
inline constexpr double smart_step = 15;
inline constexpr double smart_baby_step = 5;
inline constexpr double smart_edge = 20;
inline constexpr double smart_baby_edge = 80;

MapGeo map_geo_from_json(const Json& node, std::string name = {});
MapGeo map_geo_from_g(const Json& g, std::string_view map);
bool in_bounds(const MapGeo& geo, double x, double y, double buf = map_actor_buffer);
bool can_stand(const MapGeo& geo, double x, double y, double buf = map_actor_buffer);
bool can_move(const MapGeo& geo, double x1, double y1, double x2, double y2,
              double buf = map_actor_buffer);
std::optional<std::pair<double, double>> clamp_move(const MapGeo& geo, double x1, double y1,
                                                    double x2, double y2,
                                                    double buf = map_actor_buffer);
std::pair<double, double> calculate_move(const MapGeo& geo, double x, double y, double tx,
                                         double ty);
std::optional<std::pair<double, double>> unstuck_move(const MapGeo& geo, double x, double y,
                                                      double tx, double ty);
std::vector<std::pair<double, double>> find_path(const MapGeo& geo, double x1, double y1,
                                                 double x2, double y2,
                                                 double step = smart_step, int max_nodes = 40000);

}  // namespace caracal
