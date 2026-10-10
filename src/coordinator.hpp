#pragma once

#include "json.hpp"

#include <atomic>
#include <string>

namespace caracal {

int run_coordinator(const std::string& config_path);

}  // namespace caracal
