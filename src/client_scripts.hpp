#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace caracal::client_scripts {

    std::vector<std::string> extract_script_srcs(std::string_view html);
    std::vector<std::string> resolve_game_files_from_index_html(std::string_view index_html);
    std::vector<std::string> resolve_runner_files_from_runner_html(std::string_view runner_html);

} // namespace caracal::client_scripts
