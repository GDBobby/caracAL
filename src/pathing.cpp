#include "pathing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace caracal {
namespace {

constexpr double kEps = 1e-8;
const double kReps = std::numeric_limits<double>::epsilon();

void sort2(double& a, double& b) {
  if (a > b) std::swap(a, b);
}

bool finite_pt(double x, double y) { return std::isfinite(x) && std::isfinite(y); }

const Json* geo_node(const Json& g, std::string_view map) {
  if (!g.is_object()) return nullptr;
  std::string m(map);
  if (g["geometry"].is_object() && g["geometry"].contains(m) && g["geometry"][m].is_object()) {
    return &g["geometry"][m];
  }
  if (g["maps"].is_object() && g["maps"].contains(m) && g["maps"][m].is_object()) {
    const auto& mp = g["maps"][m];
    if (mp["data"].is_object()) return &mp["data"];
    if (mp["x_lines"].is_array() || mp["y_lines"].is_array()) return &mp;
  }
  return nullptr;
}

/* Inner can_move(..., based=1) from js/common_functions.js — center point vs lines, no pad. */
bool can_move_segment(const MapGeo& geo, double x0, double y0, double x1, double y1) {
  if (!finite_pt(x0, y0) || !finite_pt(x1, y1)) return false;
  double minx = std::min(x0, x1), maxx = std::max(x0, x1);
  double miny = std::min(y0, y1), maxy = std::max(y0, y1);
  for (const auto& line : geo.x_lines) {
    if (line.x == x1 &&
        ((line.y1 <= y1 && y1 <= line.y2) || (line.x == x0 && y0 <= line.y1 && y1 > line.y1))) {
      return false;
    }
    if (minx > line.x) continue;
    if (maxx < line.x) break;
    double next = y0 + (y1 - y0) * (line.x - x0) / (x1 - x0 + kReps);
    if (!(line.y1 - kEps <= next && next <= line.y2 + kEps)) continue;
    return false;
  }
  for (const auto& line : geo.y_lines) {
    if (line.y == y1 &&
        ((line.x1 <= x1 && x1 <= line.x2) || (line.y == y0 && x0 <= line.x1 && x1 > line.x1))) {
      return false;
    }
    if (miny > line.y) continue;
    if (maxy < line.y) break;
    double next = x0 + (x1 - x0) * (line.y - y0) / (y1 - y0 + kReps);
    if (!(line.x1 - kEps <= next && next <= line.x2 + kEps)) continue;
    return false;
  }
  return true;
}

uint64_t qkey(double x, double y) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(std::lround(x)))) << 32) |
         static_cast<uint32_t>(static_cast<int32_t>(std::lround(y)));
}

}  // namespace

MapGeo map_geo_from_json(const Json& node, std::string name) {
  MapGeo g;
  g.name = std::move(name);
  if (!node.is_object()) return g;
  if (node["min_x"].is_number() && node["max_x"].is_number() && node["min_y"].is_number() &&
      node["max_y"].is_number()) {
    g.min_x = node["min_x"].as_number();
    g.max_x = node["max_x"].as_number();
    g.min_y = node["min_y"].as_number();
    g.max_y = node["max_y"].as_number();
    g.has_bounds = true;
  }
  if (node["x_lines"].is_array()) {
    for (const auto& ln : node["x_lines"].as_array()) {
      if (!ln.is_array() || ln.as_array().size() < 3) continue;
      if (!ln[0].is_number() || !ln[1].is_number() || !ln[2].is_number()) continue;
      XLine x;
      x.x = ln[0].as_number();
      x.y1 = ln[1].as_number();
      x.y2 = ln[2].as_number();
      sort2(x.y1, x.y2);
      g.x_lines.push_back(x);
    }
  }
  if (node["y_lines"].is_array()) {
    for (const auto& ln : node["y_lines"].as_array()) {
      if (!ln.is_array() || ln.as_array().size() < 3) continue;
      if (!ln[0].is_number() || !ln[1].is_number() || !ln[2].is_number()) continue;
      YLine y;
      y.y = ln[0].as_number();
      y.x1 = ln[1].as_number();
      y.x2 = ln[2].as_number();
      sort2(y.x1, y.x2);
      g.y_lines.push_back(y);
    }
  }
  std::sort(g.x_lines.begin(), g.x_lines.end(),
            [](const XLine& a, const XLine& b) { return a.x < b.x; });
  std::sort(g.y_lines.begin(), g.y_lines.end(),
            [](const YLine& a, const YLine& b) { return a.y < b.y; });
  g.ok = !g.x_lines.empty() || !g.y_lines.empty() || g.has_bounds;
  return g;
}

MapGeo map_geo_from_g(const Json& g, std::string_view map) {
  auto* n = geo_node(g, map);
  if (!n) return {};
  return map_geo_from_json(*n, std::string(map));
}

bool in_bounds(const MapGeo& geo, double x, double y, double buf) {
  if (!geo.has_bounds) return true;
  return x >= geo.min_x + buf && x <= geo.max_x - buf && y >= geo.min_y + buf &&
         y <= geo.max_y - buf;
}

bool can_move(const MapGeo& geo, double x0, double y0, double x1, double y1, double /*buf*/) {
  if (!finite_pt(x0, y0) || !finite_pt(x1, y1)) return false;
  if (!geo.ok) return std::hypot(x1 - x0, y1 - y0) <= smart_step;
  /* set_base + 4-corner and dest-fence checks from can_move() */
  static constexpr double kOff[4][2] = {{-actor_h, actor_vn},
                                        {actor_h, actor_vn},
                                        {-actor_h, -actor_v},
                                        {actor_h, -actor_v}};
  for (int i = 0; i < 4; ++i) {
    double mx = kOff[i][0], my = kOff[i][1];
    if (!can_move_segment(geo, x0 + mx, y0 + my, x1 + mx, y1 + my)) return false;
  }
  double px0 = actor_h, px1 = -actor_h;
  if (x1 > x0) {
    px0 = -actor_h;
    px1 = actor_h;
  }
  double py0 = actor_vn, py1 = -actor_v;
  if (y1 > y0) {
    py0 = -actor_v;
    py1 = actor_vn;
  }
  if (!can_move_segment(geo, x1 + px1, y1 + py0, x1 + px1, y1 + py1)) return false;
  if (!can_move_segment(geo, x1 + px0, y1 + py1, x1 + px1, y1 + py1)) return false;
  return true;
}

bool can_stand(const MapGeo& geo, double x, double y, double /*buf*/) {
  if (!finite_pt(x, y)) return false;
  if (!geo.ok) return true;
  return can_move(geo, x, y, x, y + 1e-6);
}

namespace {

bool eps_equal(double a, double b) { return std::abs(a - b) < 5 * kEps; }

/* calculate_movex() from js/common_functions.js */
std::pair<double, double> calculate_movex(const MapGeo& geo, double cur_x, double cur_y,
                                          double target_x, double target_y) {
  bool going_down = cur_y < target_y;
  bool going_right = cur_x < target_x;
  double min_x = std::min(cur_x, target_x), max_x = std::max(cur_x, target_x);
  double min_y = std::min(cur_y, target_y), max_y = std::max(cur_y, target_y);
  double dx = target_x - cur_x, dy = target_y - cur_y;
  double dydx = dy / (dx + kReps);
  double dxdy = 1.0 / dydx;
  const double xeps = 10 * kEps;

  for (const auto& line : geo.x_lines) {
    double line_x = line.x, line_xE = going_right ? line_x - xeps : line_x + xeps;
    if (max_x < line_x) break;
    if (min_x > line_x || max_y < line.y1 || min_y > line.y2) continue;
    double y_intersect = cur_y + (line_x - cur_x) * dydx;
    if (eps_equal(cur_x, target_x) && eps_equal(cur_x, line_x)) {
      if (going_down) {
        y_intersect = std::min(line.y1, line.y2) - xeps;
        target_y = std::min(target_y, y_intersect);
        max_y = target_y;
      } else {
        y_intersect = std::max(line.y1, line.y2) + xeps;
        target_y = std::min(target_y, y_intersect);
        min_y = target_y;
      }
      continue;
    }
    if (y_intersect < line.y1 || y_intersect > line.y2) continue;
    if (going_down) {
      target_y = std::min(target_y, y_intersect);
      max_y = target_y;
    } else {
      target_y = std::max(target_y, y_intersect);
      min_y = target_y;
    }
    if (going_right) {
      target_x = std::min(target_x, line_xE);
      max_x = target_x;
    } else {
      target_x = std::max(target_x, line_xE);
      min_x = target_x;
    }
  }
  for (const auto& line : geo.y_lines) {
    double line_y = line.y, line_yE = going_down ? line_y - xeps : line_y + xeps;
    if (max_y < line_y) break;
    if (min_y > line_y || max_x < line.x1 || min_x > line.x2) continue;
    double x_intersect = cur_x + (line_y - cur_y) * dxdy;
    if (eps_equal(cur_y, target_y) && eps_equal(cur_y, line_y)) {
      if (going_right) {
        x_intersect = std::min(line.x1, line.x2) - xeps;
        target_x = std::min(target_x, x_intersect);
        max_x = target_x;
      } else {
        x_intersect = std::max(line.x1, line.x2) + xeps;
        target_x = std::min(target_x, x_intersect);
        min_x = target_x;
      }
      continue;
    }
    if (x_intersect < line.x1 || x_intersect > line.x2) continue;
    if (going_right) {
      target_x = std::min(target_x, x_intersect);
      max_x = target_x;
    } else {
      target_x = std::max(target_x, x_intersect);
      min_x = target_x;
    }
    if (going_down) {
      target_y = std::min(target_y, line_yE);
      max_y = target_y;
    } else {
      target_y = std::max(target_y, line_yE);
      min_y = target_y;
    }
  }
  return {target_x, target_y};
}

bool has_xy(const std::vector<std::pair<double, double>>& moves, double x, double y) {
  for (auto [mx, my] : moves) {
    if (std::hypot(mx - x, my - y) < 1e-6) return true;
  }
  return false;
}

}  // namespace

std::pair<double, double> calculate_move(const MapGeo& geo, double cur_x, double cur_y, double tx,
                                         double ty) {
  if (!geo.ok || !finite_pt(cur_x, cur_y) || !finite_pt(tx, ty)) return {tx, ty};
  static constexpr double kOff[4][2] = {{-actor_h, actor_vn},
                                        {actor_h, actor_vn},
                                        {-actor_h, -actor_v},
                                        {actor_h, -actor_v}};
  std::vector<std::pair<double, double>> moves{{tx, ty}};
  for (int c = 0; c < 4; ++c) {
    double mx = kOff[c][0], my = kOff[c][1];
    for (int i = 0; i < 3; ++i) {
      double dx = tx + mx, dy = ty + my;
      if (i == 1) dx = cur_x + mx;
      if (i == 2) dy = cur_y + my;
      auto cm = calculate_movex(geo, cur_x + mx, cur_y + my, dx, dy);
      double ox = cm.first - mx, oy = cm.second - my;
      if (!has_xy(moves, ox, oy)) moves.emplace_back(ox, oy);
    }
  }
  std::pair<double, double> best{cur_x, cur_y};
  double best_d = 1e100;
  for (auto [x, y] : moves) {
    if (!can_move(geo, cur_x, cur_y, x, y)) continue;
    double d = std::hypot(tx - x, ty - y);
    if (d < best_d) {
      best_d = d;
      best = {x, y};
    }
  }
  if (std::hypot(best.first - cur_x, best.second - cur_y) < 10 * kEps) return {cur_x, cur_y};
  return best;
}

std::optional<std::pair<double, double>> unstuck_move(const MapGeo& geo, double x, double y,
                                                      double tx, double ty) {
  if (!finite_pt(x, y)) return std::nullopt;
  static constexpr double kTry[16][2] = {
      {-8.1, 0}, {8.1, 0},  {0, -8.1}, {0, 8.1},  {-12, 0}, {12, 0},  {0, -12}, {0, 12},
      {-16, 0},  {16, 0},   {0, -16},  {0, 16},   {-8.1, -8.1}, {-8.1, 8.1}, {8.1, -8.1}, {8.1, 8.1}};
  std::optional<std::pair<double, double>> best;
  double best_d = 1e100;
  for (int i = 0; i < 16; ++i) {
    double nx = x + kTry[i][0], ny = y + kTry[i][1];
    if (!can_stand(geo, nx, ny)) continue;
    if (!can_move_segment(geo, x, y, nx, ny)) continue;
    double d = std::hypot(tx - nx, ty - ny);
    if (d < best_d) {
      best_d = d;
      best = std::pair{nx, ny};
    }
  }
  return best;
}

std::optional<std::pair<double, double>> clamp_move(const MapGeo& geo, double x1, double y1,
                                                    double x2, double y2, double buf) {
  if (!finite_pt(x1, y1) || !finite_pt(x2, y2)) return std::nullopt;
  if (can_move(geo, x1, y1, x2, y2, buf)) return std::pair{x2, y2};
  if (!geo.ok) {
    double d = std::hypot(x2 - x1, y2 - y1);
    if (d < 1) return std::nullopt;
    double s = std::min(1.0, 20.0 / d);
    return std::pair{x1 + (x2 - x1) * s, y1 + (y2 - y1) * s};
  }
  double lo = 0, hi = 1;
  double bx = x1, by = y1;
  for (int i = 0; i < 18; ++i) {
    double mid = (lo + hi) / 2;
    double mx = x1 + (x2 - x1) * mid;
    double my = y1 + (y2 - y1) * mid;
    if (can_move(geo, x1, y1, mx, my, buf)) {
      lo = mid;
      bx = mx;
      by = my;
    } else {
      hi = mid;
    }
  }
  if (std::hypot(bx - x1, by - y1) < 1) return std::nullopt;
  return std::pair{bx, by};
}

namespace {

void smooth_plot(const MapGeo& geo, std::vector<std::pair<double, double>>& plot) {
  std::size_t i = 0;
  while (i + 2 < plot.size()) {
    if (can_move(geo, plot[i].first, plot[i].second, plot[i + 2].first, plot[i + 2].second)) {
      plot.erase(plot.begin() + static_cast<std::ptrdiff_t>(i + 1));
      continue;
    }
    ++i;
  }
}

}  // namespace

std::vector<std::pair<double, double>> find_path(const MapGeo& geo, double x1, double y1,
                                                 double x2, double y2, double step,
                                                 int max_nodes) {
  if (!finite_pt(x1, y1) || !finite_pt(x2, y2)) return {};
  if (std::hypot(x2 - x1, y2 - y1) < 1) return {};
  if (can_move(geo, x1, y1, x2, y2)) return {{x2, y2}};
  if (!geo.ok) return {};

  double big = step > 0 ? step : smart_step;
  double baby = std::max(1.0, big / 3.0);

  struct Node {
    double x = 0;
    double y = 0;
    int parent = -1;
  };
  std::vector<Node> queue;
  std::vector<double> gscore;
  std::unordered_set<uint64_t> visited;
  auto manhattan = [&](double x, double y) { return std::abs(x - x2) + std::abs(y - y2); };
  auto push_node = [&](double x, double y, int parent, double g) -> int {
    uint64_t k = qkey(x, y);
    if (visited.contains(k)) return -1;
    visited.insert(k);
    int idx = static_cast<int>(queue.size());
    queue.push_back(Node{x, y, parent});
    gscore.push_back(g);
    return idx;
  };
  push_node(x1, y1, -1, 0);

  using Qe = std::pair<double, int>;
  std::priority_queue<Qe, std::vector<Qe>, std::greater<Qe>> open;
  open.push({manhattan(x1, y1), 0});

  auto expand = [&](int idx, double mag) {
    static constexpr double kDx[4] = {0, 0, 1, -1};
    static constexpr double kDy[4] = {1, -1, 0, 0};
    Node cur = queue[idx];
    int n0 = static_cast<int>(queue.size());
    for (int d = 0; d < 4; ++d) {
      double nx = cur.x + kDx[d] * mag;
      double ny = cur.y + kDy[d] * mag;
      if (!can_move(geo, cur.x, cur.y, nx, ny)) continue;
      int nidx = push_node(nx, ny, idx, gscore[idx] + mag);
      if (nidx >= 0) open.push({gscore[nidx] + manhattan(nx, ny), nidx});
    }
    return static_cast<int>(queue.size()) > n0;
  };

  if (!expand(0, baby) && !expand(0, big)) {
    static constexpr double kU[12][2] = {
        {-8.1, 0}, {8.1, 0}, {0, -8.1}, {0, 8.1}, {-16, 0}, {16, 0},
        {0, -16},  {0, 16},  {-16, -16}, {-16, 16}, {16, -16}, {16, 16}};
    for (int u = 0; u < 12; ++u) {
      double nx = x1 + kU[u][0], ny = y1 + kU[u][1];
      if (!can_stand(geo, nx, ny)) continue;
      int parent = can_move(geo, x1, y1, nx, ny) ? 0 : -1;
      int nidx = push_node(nx, ny, parent, std::hypot(kU[u][0], kU[u][1]));
      if (nidx >= 0) open.push({gscore[nidx] + manhattan(nx, ny), nidx});
    }
  }

  int found = -1;
  while (!open.empty() && static_cast<int>(queue.size()) < max_nodes) {
    auto [f, idx] = open.top();
    open.pop();
    (void)f;
    const Node& cur = queue[idx];
    double cdist = manhattan(cur.x, cur.y);
    if (cdist < smart_edge) {
      found = idx;
      break;
    }
    double mag = cdist < smart_baby_edge ? baby : big;
    expand(idx, mag);
  }

  if (found < 0) return {};
  std::vector<std::pair<double, double>> path;
  for (int i = found; i >= 0; i = queue[i].parent) {
    path.emplace_back(queue[i].x, queue[i].y);
  }
  std::reverse(path.begin(), path.end());
  if (!path.empty() && can_move(geo, path.back().first, path.back().second, x2, y2)) {
    path.emplace_back(x2, y2);
  }
  smooth_plot(geo, path);
  if (!path.empty() && std::hypot(path.front().first - x1, path.front().second - y1) < 1) {
    path.erase(path.begin());
  }
  return path;
}

}  // namespace caracal
