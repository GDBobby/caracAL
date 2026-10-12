#pragma once

#include <chrono>
#include <string_view>

namespace caracal::constants {

    inline constexpr std::string_view coordinator_module_path = "./standalones/CharacterCoordinator.js";
    inline constexpr std::string_view config_example_path = "./config.EXAMPLE.json";
    inline constexpr std::string_view localstorage_path = "./localStorage/caraGarage.jsonl";
    inline constexpr std::string_view localstorage_rota_path = "./localStorage/caraGarage.other.jsonl";

    inline constexpr auto stat_beat_interval = std::chrono::milliseconds(500);
    inline constexpr auto stale_stat_beat = std::chrono::milliseconds(30'000);
    inline constexpr auto char_watchdog_interval = std::chrono::milliseconds(5'000);

    inline constexpr int privileged_port_ceiling = 1024;
    inline constexpr int default_web_panel_port_windows = 924;
    inline constexpr int default_web_panel_port_unix = 1924;

} // namespace caracal::constants
