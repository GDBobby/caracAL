#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace caracal {

inline constexpr int load_deadline_min_ms = 14'000;
inline constexpr int load_deadline_max_ms = 60'000;
inline constexpr int load_deadline_base_ms = load_deadline_min_ms;
inline constexpr int load_deadline_rtt_factor = 12;
inline constexpr int probe_timeout_ms = 5'000;

struct RealmHost {
  std::string host;
  std::optional<int> port;
};

RealmHost parse_realm_host(std::string_view realm_host);
int probe_realm_rtt_ms(const std::string& host, int port, bool tls,
                       int timeout_ms = probe_timeout_ms);
int load_deadline_ms_from_rtt(double rtt_ms,
                              int min_ms = load_deadline_min_ms,
                              int max_ms = load_deadline_max_ms,
                              int base_ms = load_deadline_base_ms,
                              int factor = load_deadline_rtt_factor);
bool should_refresh_client_for_welcome(double local_version, double welcome_version);

}  // namespace caracal
