#pragma once

#include <string_view>

namespace caracal {

// Emergency fallback when html_globals.js could not be extracted/cached.
inline constexpr std::string_view html_prelude_source = R"AL(
var last_deploy = "";
var proximity_guides = false;
var update_notes = [];
var update_notes_more = false;
)AL";

}  // namespace caracal
