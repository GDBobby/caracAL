#pragma once

#include <string>
#include <vector>

namespace caracal::stream_multiplexer {

struct Child {
  int pid = -1;
  int stdin_fd = -1;
};

Child spawn_sink(const std::vector<std::string>& command);

// Forks `module_path` with args, tees stdout/stderr into each log sink without
// applying backpressure to the producer.
int setup_log_pipes(const std::vector<std::vector<std::string>>& log_sinks,
                    const std::string& module_path,
                    const std::vector<std::string>& args);

}  // namespace caracal::stream_multiplexer
