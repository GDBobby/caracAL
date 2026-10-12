#include "monitoring_util.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>

namespace caracal::monitoring {
namespace {

bool pot_match(const Json& it, const Json& G, std::string_view kind) {
  auto name = it["name"].as_string_opt();
  if (!name) return false;
  Json g_item = Json(nullptr);
  if (G.is_object() && G["items"].is_object() && G["items"].contains(*name)) {
    g_item = G["items"][*name];
  }
  if (kind == "hp") {
    return name->starts_with("hpot") || item_gives(g_item, "hp");
  }
  return name->starts_with("mpot") || item_gives(g_item, "mp");
}

}  // namespace

std::optional<Json> slim_item_instance(const Json& item) {
  if (!item.is_object()) return std::nullopt;
  auto name = item["name"].as_string_opt();
  if (!name) return std::nullopt;
  Json out = Json::object();
  out["name"] = *name;
  if (item["level"].is_number()) out["level"] = item["level"].as_number();
  if (item["q"].is_number()) out["q"] = item["q"].as_number();
  if (!item["p"].is_null() && item["p"].truthy()) out["p"] = item["p"];
  return out;
}

bool item_gives(const Json& g_item, std::string_view stat) {
  if (!g_item.is_object() || !g_item["gives"].is_array()) return false;
  for (const auto& g : g_item["gives"].as_array()) {
    if (g.is_array() && !g.as_array().empty() &&
        g[0].as_string_opt() == std::optional<std::string>{std::string(stat)}) {
      return true;
    }
  }
  return false;
}

std::optional<Json> best_pot_instance(const Json& items, const Json& G,
                                      std::string_view kind) {
  if (!items.is_array()) return std::nullopt;
  std::optional<Json> best;
  double best_score = -1;
  for (const auto& it : items.as_array()) {
    if (!pot_match(it, G, kind)) continue;
    double score = it["q"].as_number_opt().value_or(1);
    if (score > best_score) {
      best_score = score;
      best = slim_item_instance(it);
    }
  }
  return best;
}

int find_pot_slot(const Json& items, const Json& G, std::string_view kind) {
  if (!items.is_array()) return -1;
  const auto& a = items.as_array();
  for (int i = static_cast<int>(a.size()) - 1; i >= 0; --i) {
    if (!a[static_cast<std::size_t>(i)].is_object()) continue;
    if (pot_match(a[static_cast<std::size_t>(i)], G, kind)) return i;
  }
  return -1;
}

Json pot_summary(const Json& items, const Json& G, std::string_view kind) {
  Json out = Json::object();
  out["total"] = 0;
  if (!items.is_array()) return out;
  std::optional<Json> best;
  double best_score = -1;
  double total = 0;
  for (const auto& it : items.as_array()) {
    if (!pot_match(it, G, kind)) continue;
    double q = it["q"].as_number_opt().value_or(1);
    total += q;
    if (q > best_score) {
      best_score = q;
      best = slim_item_instance(it);
    }
  }
  out["total"] = total;
  if (best) out["best"] = *best;
  else out["best"] = nullptr;
  return out;
}

Json favorite_item_strip(const Json& character, const Json& G) {
  Json strip = Json::array();
  Json slots = character.is_object() && character["slots"].is_object()
                   ? character["slots"]
                   : Json::object();
  if (auto mh = slim_item_instance(slots["mainhand"])) strip.as_array().push_back(*mh);
  if (auto oh = slim_item_instance(slots["offhand"])) strip.as_array().push_back(*oh);
  Json items = character.is_object() ? character["items"] : Json(nullptr);
  if (auto hp = best_pot_instance(items, G, "hp")) strip.as_array().push_back(*hp);
  if (auto mp = best_pot_instance(items, G, "mp")) strip.as_array().push_back(*mp);
  return strip;
}

Json inventory_item_grid(const Json& character) {
  Json items = character.is_object() && character["items"].is_array() ? character["items"]
                                                                     : Json::array();
  int slots = 42;
  if (character.is_object() && character["isize"].is_number()) {
    slots = static_cast<int>(character["isize"].as_number());
  }
  Json out = Json::array();
  for (int i = 0; i < slots; ++i) {
    if (items.is_array() && static_cast<std::size_t>(i) < items.as_array().size()) {
      auto slim = slim_item_instance(items[static_cast<std::size_t>(i)]);
      out.as_array().push_back(slim ? *slim : Json(nullptr));
    } else {
      out.as_array().push_back(Json(nullptr));
    }
  }
  return out;
}

Json slim_trade_listings_from_character(const Json& character) {
  Json slots = character.is_object() && character["slots"].is_object()
                   ? character["slots"]
                   : Json::object();
  std::vector<std::string> keys;
  for (const auto& [k, _] : slots.as_object()) {
    if (k.starts_with("trade")) keys.push_back(k);
  }
  std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
    return a < b;
  });
  Json out = Json::array();
  for (const auto& key : keys) {
    const Json& it = slots[key];
    if (!it.is_object() || !it["name"].is_string()) continue;
    if (it["giveaway"].truthy()) continue;
    Json want = Json(nullptr);
    if (!it["b"].truthy() && !it["want"].is_null()) {
      if (it["want"].is_string() && !it["want"].as_string().empty()) {
        want = Json::object();
        want["name"] = it["want"].as_string();
      } else if (it["want"].is_object() && it["want"]["name"].is_string() &&
                 !it["want"]["name"].as_string().empty()) {
        want = Json::object();
        want["name"] = it["want"]["name"].as_string();
        if (it["want"]["level"].is_number()) want["level"] = it["want"]["level"].as_number();
        if (it["want"]["q"].is_number()) want["q"] = it["want"]["q"].as_number();
        if (it["want"]["p"].is_string() && !it["want"]["p"].as_string().empty())
          want["p"] = it["want"]["p"].as_string();
      }
    }
    bool has_gold = it["price"].is_number() && std::isfinite(it["price"].as_number());
    if (!has_gold && want.is_null()) continue;
    Json row = Json::object();
    row["slot"] = key;
    row["side"] = it["b"].truthy() ? "buy" : (!want.is_null() ? "swap" : "sell");
    row["name"] = it["name"].as_string();
    row["showQuantity"] = it["q"].is_number() && it["q"].as_number() > 1;
    if (has_gold) row["price"] = it["price"].as_number();
    if (it["level"].is_number()) row["level"] = it["level"].as_number();
    if (it["q"].is_number()) row["q"] = it["q"].as_number();
    if (!it["p"].is_null() && it["p"].truthy()) row["p"] = it["p"];
    if (!want.is_null()) row["want"] = want;
    out.as_array().push_back(row);
  }
  return out;
}

Json slim_gear_grid_from_character(const Json& character) {
  static const char* order[] = {
      "earring1", "helmet", "earring2", "amulet", "mainhand", "chest", "offhand", "cape",
      "ring1",    "pants",  "ring2",    "orb",    "belt",     "shoes", "gloves",  "elixir",
  };
  Json slots = character.is_object() && character["slots"].is_object()
                   ? character["slots"]
                   : Json::object();
  Json out = Json::array();
  for (const char* key : order) {
    auto slim = slim_item_instance(slots[key]);
    if (slim) (*slim)["slot"] = key;
    out.as_array().push_back(slim ? *slim : Json(nullptr));
  }
  return out;
}

int count_occupied_items(const Json& list) {
  if (!list.is_array()) return 0;
  int n = 0;
  for (const auto& it : list.as_array()) {
    if (it.is_object() && it["name"].is_string()) ++n;
  }
  return n;
}

int count_trade_slots(const Json& character) {
  if (!character.is_object() || !character["slots"].is_object()) return 0;
  int n = 0;
  for (const auto& [k, _] : character["slots"].as_object()) {
    if (k.starts_with("trade")) ++n;
  }
  return n;
}

std::string humanize_int(double num, int digits) {
  num = std::round(num);
  struct Item {
    double value;
    const char* symbol;
  };
  static const Item lookup[] = {
      {1e3, ""}, {1e6, "k"}, {1e9, "Mil"}, {1e12, "Bil"}, {1e15, "Tril"},
  };
  const Item* item = nullptr;
  for (const auto& it : lookup) {
    if (std::abs(num) < it.value) {
      item = &it;
      break;
    }
  }
  if (!item) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(digits) << num;
    return os.str();
  }
  double v = (num * 1e3) / item->value;
  std::ostringstream os;
  os << std::fixed << std::setprecision(digits) << v;
  std::string s = os.str();
  static const std::regex rx(R"(\.0+$|(\.[0-9]*[1-9])0+$)");
  s = std::regex_replace(s, rx, "$1");
  s += item->symbol;
  return s;
}

}  // namespace caracal::monitoring
