#pragma once

#include "json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace caracal::monitoring {

std::optional<Json> slim_item_instance(const Json& item);
bool item_gives(const Json& g_item, std::string_view stat);
std::optional<Json> best_pot_instance(const Json& items, const Json& G,
                                      std::string_view kind);
Json pot_summary(const Json& items, const Json& G, std::string_view kind);
Json favorite_item_strip(const Json& character, const Json& G);
Json inventory_item_grid(const Json& character);
Json slim_trade_listings_from_character(const Json& character);
Json slim_gear_grid_from_character(const Json& character);
int count_occupied_items(const Json& list);
int count_trade_slots(const Json& character);
std::string humanize_int(double num, int digits = 1);

}  // namespace caracal::monitoring
