#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace caracal::game_files {

bool follows_latest_client(const std::optional<Json>& version);
bool follows_latest_client_any(const Json& version);

std::vector<std::string> get_runner_files_fallback();
std::vector<std::string> get_game_files_fallback();
std::vector<std::string> get_game_files(std::string_view base_url = {},
                                        std::string_view version = {});
std::vector<std::string> get_runner_files(std::string_view base_url = {},
                                          std::string_view version = {});
std::vector<std::string> get_all_client_files(std::string_view base_url,
                                              std::string_view version);

std::string hostname(std::string_view base_url);
std::string locate_game_file(std::string_view base_url, std::string_view resource,
                             std::string_view version);
std::string locate_html_globals(std::string_view base_url, std::string_view version);
std::string locate_client_scripts_manifest(std::string_view base_url,
                                           std::string_view version);
std::string resolve_html_globals_source(std::string_view base_url,
                                        std::string_view version);
std::string official_download_url(std::string_view base_url, std::string_view resource);

std::string ensure_html_globals(std::string_view base_url, std::string_view version,
                                std::optional<std::string> html = std::nullopt);
int ensure_latest(std::string_view base_url);
void cull_versions(std::string_view base_url, const std::vector<int>& exclusions);
std::vector<int> available_versions(std::string_view base_url);

}  // namespace caracal::game_files
