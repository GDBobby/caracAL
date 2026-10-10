#include "stream_multiplexer.hpp"

#include "log_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace caracal::stream_multiplexer {
namespace {

void set_cloexec(int fd) {
  int flags = ::fcntl(fd, F_GETFD);
  if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_nonblock(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::vector<char*> argv_from(const std::vector<std::string>& args) {
  std::vector<char*> out;
  out.reserve(args.size() + 1);
  for (auto& a : args) out.push_back(const_cast<char*>(a.c_str()));
  out.push_back(nullptr);
  return out;
}

Child spawn_with_stdio(const std::vector<std::string>& command, bool pipe_stdout) {
  if (command.empty()) throw std::runtime_error("empty command");
  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (::pipe(in_pipe) != 0) throw std::runtime_error("pipe failed");
  if (pipe_stdout) {
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
      throw std::runtime_error("pipe failed");
    }
  }
  pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    if (pipe_stdout) {
      ::dup2(out_pipe[1], STDOUT_FILENO);
      ::dup2(err_pipe[1], STDERR_FILENO);
      ::close(out_pipe[0]);
      ::close(out_pipe[1]);
      ::close(err_pipe[0]);
      ::close(err_pipe[1]);
    }
    auto argv = argv_from(command);
    ::execvp(command[0].c_str(), argv.data());
    _exit(127);
  }
  ::close(in_pipe[0]);
  Child c;
  c.pid = pid;
  c.stdin_fd = in_pipe[1];
  set_nonblock(c.stdin_fd);
  if (pipe_stdout) {
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    // Stash stdout/stderr on unused fields via thread capture below.
    c.stdin_fd = in_pipe[1];
    (void)out_pipe;
    (void)err_pipe;
  }
  set_cloexec(c.stdin_fd);
  return c;
}

void pump_no_backpressure(int src, const std::vector<int>& dests) {
  char buf[4096];
  while (true) {
    ssize_t n = ::read(src, buf, sizeof(buf));
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (int d : dests) {
      if (d < 0) continue;
      ssize_t off = 0;
      while (off < n) {
        ssize_t w = ::write(d, buf + off, static_cast<std::size_t>(n - off));
        if (w < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EPIPE) break;
          if (errno == EINTR) continue;
          break;
        }
        off += w;
      }
    }
  }
}

}  // namespace

Child spawn_sink(const std::vector<std::string>& command) {
  return spawn_with_stdio(command, false);
}

int setup_log_pipes(const std::vector<std::vector<std::string>>& log_sinks,
                    const std::string& module_path,
                    const std::vector<std::string>& args) {
  std::vector<Child> sinks;
  for (const auto& cmd : log_sinks) {
    if (cmd.empty()) continue;
    sinks.push_back(spawn_sink(cmd));
  }

  int out_pipe[2];
  int err_pipe[2];
  if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
    throw std::runtime_error("pipe failed");
  }
  pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    std::vector<std::string> argv_s;
    argv_s.push_back(module_path);
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    auto argv = argv_from(argv_s);
    ::execvp(module_path.c_str(), argv.data());
    _exit(127);
  }
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);

  std::vector<int> dests;
  for (auto& s : sinks) dests.push_back(s.stdin_fd);
  if (dests.empty()) {
    dests.push_back(STDOUT_FILENO);
  }

  std::thread t_out([out = out_pipe[0], dests] { pump_no_backpressure(out, dests); });
  std::thread t_err([err = err_pipe[0], dests] { pump_no_backpressure(err, dests); });
  t_out.detach();
  t_err.detach();
  log().info("log pipes attached to " + module_path);
  return pid;
}

}  // namespace caracal::stream_multiplexer
