#pragma once

#include "json.hpp"

#include <string>
#include <string_view>

namespace caracal {

Json load_g_data(std::string_view path);
Json try_parse_data_js(std::string_view src);

}  // namespace caracal
