#pragma once

#include <string>
#include <string_view>

namespace caracal {

std::string extract_page_globals(std::string_view html);

}
