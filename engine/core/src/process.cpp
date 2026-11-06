#include "paperboat/core/process.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace paperboat::core {
namespace {

namespace fs = std::filesystem;

static std::atomic<pid_t> g_active_child_pgid{0};

void signal_handler(int sig) {
  pid_t pgid = g_active_child_pgid.load();
  if (pgid > 1) {
    kill(-pgid, sig);
  }
}

// Build environment vector for execvpe / execve
std::vector<std::string> prepare_env(const std::vector<std::pair<std::string, std::string>>& custom_env) {
  std::vector<std::string> env_strings;
  std::vector<std::string> overridden;
  for (const auto& [k, v] : custom_env) {
    overridden.push_back(k);
    env_strings.push_back(k + "=" + v);
  }

  if (environ) {
    for (char** env = environ; *env != nullptr; ++env) {
      std::string entry = *env;
      auto eq = entry.find('=');
      if (eq != std::string::npos) {
        std::string key = entry.substr(0, eq);
        bool found = false;
        for (const auto& ok : overridden) {
          if (ok == key) {
            found = true;
            break;
          }
        }
        if (!found) {
          env_strings.push_back(entry);
        }
      }
    }
  }
  return env_strings;
}

std::string trim_trailing_newlines(std::string_view s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

void process_buffer_lines(std::string& buffer, std::string_view stream_name, const ProcessOutputCallback& cb) {
  std::size_t pos = 0;
  while ((pos = buffer.find('\n')) != std::string::npos) {
    std::string line = buffer.substr(0, pos);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    if (cb) cb(stream_name, line);
    buffer.erase(0, pos + 1);
  }
}

}  // namespace

ProcessResult ProcessRunner::run_sync(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& env,
    std::chrono::milliseconds timeout) {
  ProcessResult result;
  if (args.empty()) return result;

  int pipe_out[2];
  int pipe_err[2];
  if (pipe(pipe_out) != 0 || pipe(pipe_err) != 0) {
    result.exit_code = -1;
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    result.exit_code = -1;
    return result;
  }

  if (pid == 0) {
    // Child
    setpgid(0, 0);
    close(pipe_out[0]);
    close(pipe_err[0]);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_out[1]);
    close(pipe_err[1]);

    if (!cwd.empty()) {
      std::error_code ec;
      fs::current_path(cwd, ec);
    }

    std::vector<std::string> env_strings = prepare_env(env);
    std::vector<char*> envp;
    envp.reserve(env_strings.size() + 1);
    for (auto& s : env_strings) envp.push_back(s.data());
    envp.push_back(nullptr);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    execvpe(argv[0], argv.data(), envp.data());
    _exit(127);
  }

  // Parent
  close(pipe_out[1]);
  close(pipe_err[1]);

  fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
  fcntl(pipe_err[0], F_SETFL, O_NONBLOCK);

  std::array<pollfd, 2> fds{};
  fds[0].fd = pipe_out[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipe_err[0];
  fds[1].events = POLLIN;

  auto start_time = std::chrono::steady_clock::now();
  bool out_closed = false;
  bool err_closed = false;

  std::array<char, 4096> buf{};
  while (!out_closed || !err_closed) {
    if (timeout.count() > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time);
      if (elapsed >= timeout) {
        result.timed_out = true;
        kill_pgid(pid, SIGKILL, 500);
        break;
      }
    }

    int ret = poll(fds.data(), 2, 100);
    if (ret < 0 && errno == EINTR) continue;

    if (ret > 0) {
      if (!out_closed && (fds[0].revents & POLLIN)) {
        ssize_t n = read(pipe_out[0], buf.data(), buf.size());
        if (n > 0) result.stdout_text.append(buf.data(), static_cast<std::size_t>(n));
      }
      if (!out_closed && (fds[0].revents & (POLLHUP | POLLERR))) {
        ssize_t n = 0;
        while ((n = read(pipe_out[0], buf.data(), buf.size())) > 0) {
          result.stdout_text.append(buf.data(), static_cast<std::size_t>(n));
        }
        close(pipe_out[0]);
        out_closed = true;
        fds[0].fd = -1;
      }

      if (!err_closed && (fds[1].revents & POLLIN)) {
        ssize_t n = read(pipe_err[0], buf.data(), buf.size());
        if (n > 0) result.stderr_text.append(buf.data(), static_cast<std::size_t>(n));
      }
      if (!err_closed && (fds[1].revents & (POLLHUP | POLLERR))) {
        ssize_t n = 0;
        while ((n = read(pipe_err[0], buf.data(), buf.size())) > 0) {
          result.stderr_text.append(buf.data(), static_cast<std::size_t>(n));
        }
        close(pipe_err[0]);
        err_closed = true;
        fds[1].fd = -1;
      }
    }

    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
      else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    }
  }

  if (!out_closed) close(pipe_out[0]);
  if (!err_closed) close(pipe_err[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) == pid) {
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
  }

  return result;
}

int ProcessRunner::run_stream(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& env,
    ProcessOutputCallback on_output,
    std::stop_token stop) {
  if (args.empty()) return -1;

  int pipe_out[2];
  int pipe_err[2];
  if (pipe(pipe_out) != 0 || pipe(pipe_err) != 0) {
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    return -1;
  }

  if (pid == 0) {
    // Put child and all descendants into a new process group
    setpgid(0, 0);
    close(pipe_out[0]);
    close(pipe_err[0]);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_out[1]);
    close(pipe_err[1]);

    if (!cwd.empty()) {
      std::error_code ec;
      fs::current_path(cwd, ec);
    }

    std::vector<std::string> env_strings = prepare_env(env);
    std::vector<char*> envp;
    envp.reserve(env_strings.size() + 1);
    for (auto& s : env_strings) envp.push_back(s.data());
    envp.push_back(nullptr);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    execvpe(argv[0], argv.data(), envp.data());
    _exit(127);
  }

  close(pipe_out[1]);
  close(pipe_err[1]);

  fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
  fcntl(pipe_err[0], F_SETFL, O_NONBLOCK);

  g_active_child_pgid.store(pid);
  struct sigaction sa_int{}, sa_term{}, old_int{}, old_term{};
  sa_int.sa_handler = signal_handler;
  sigemptyset(&sa_int.sa_mask);
  sa_term.sa_handler = signal_handler;
  sigemptyset(&sa_term.sa_mask);
  sigaction(SIGINT, &sa_int, &old_int);
  sigaction(SIGTERM, &sa_term, &old_term);

  std::array<pollfd, 2> fds{};
  fds[0].fd = pipe_out[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipe_err[0];
  fds[1].events = POLLIN;

  std::string stdout_buf;
  std::string stderr_buf;
  std::array<char, 4096> buf{};
  bool out_closed = false;
  bool err_closed = false;

  while (!out_closed || !err_closed) {
    if (stop.stop_requested()) {
      kill_pgid(pid, SIGTERM, 1000);
      break;
    }

    int ret = poll(fds.data(), 2, 100);
    if (ret < 0 && errno == EINTR) continue;

    if (ret > 0) {
      if (!out_closed && (fds[0].revents & POLLIN)) {
        ssize_t n = read(pipe_out[0], buf.data(), buf.size());
        if (n > 0) {
          stdout_buf.append(buf.data(), static_cast<std::size_t>(n));
          process_buffer_lines(stdout_buf, "stdout", on_output);
        }
      }
      if (!out_closed && (fds[0].revents & (POLLHUP | POLLERR))) {
        ssize_t n = 0;
        while ((n = read(pipe_out[0], buf.data(), buf.size())) > 0) {
          stdout_buf.append(buf.data(), static_cast<std::size_t>(n));
          process_buffer_lines(stdout_buf, "stdout", on_output);
        }
        close(pipe_out[0]);
        out_closed = true;
        fds[0].fd = -1;
      }

      if (!err_closed && (fds[1].revents & POLLIN)) {
        ssize_t n = read(pipe_err[0], buf.data(), buf.size());
        if (n > 0) {
          stderr_buf.append(buf.data(), static_cast<std::size_t>(n));
          process_buffer_lines(stderr_buf, "stderr", on_output);
        }
      }
      if (!err_closed && (fds[1].revents & (POLLHUP | POLLERR))) {
        ssize_t n = 0;
        while ((n = read(pipe_err[0], buf.data(), buf.size())) > 0) {
          stderr_buf.append(buf.data(), static_cast<std::size_t>(n));
          process_buffer_lines(stderr_buf, "stderr", on_output);
        }
        close(pipe_err[0]);
        err_closed = true;
        fds[1].fd = -1;
      }
    }

    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
  }

  // Drain remaining buffers
  if (!stdout_buf.empty() && on_output) on_output("stdout", trim_trailing_newlines(stdout_buf));
  if (!stderr_buf.empty() && on_output) on_output("stderr", trim_trailing_newlines(stderr_buf));

  if (!out_closed) close(pipe_out[0]);
  if (!err_closed) close(pipe_err[0]);

  // Clean termination of process group
  kill_pgid(pid, SIGTERM, 1000);

  int exit_code = -1;
  int status = 0;
  if (waitpid(pid, &status, 0) == pid) {
    if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
  }

  g_active_child_pgid.store(0);
  sigaction(SIGINT, &old_int, nullptr);
  sigaction(SIGTERM, &old_term, nullptr);

  return exit_code;
}

int ProcessRunner::run_detached(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& env,
    const fs::path& log_file) {
  if (args.empty()) return -1;

  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid > 0) {
    // Parent returns child pid
    return pid;
  }

  // Grandchild creates detached session
  setsid();

  int devnull = open("/dev/null", O_RDONLY);
  if (devnull >= 0) {
    dup2(devnull, STDIN_FILENO);
    close(devnull);
  }

  int out_fd = -1;
  if (!log_file.empty()) {
    std::error_code ec;
    fs::create_directories(log_file.parent_path(), ec);
    out_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  }
  if (out_fd < 0) {
    out_fd = open("/dev/null", O_WRONLY);
  }

  if (out_fd >= 0) {
    dup2(out_fd, STDOUT_FILENO);
    dup2(out_fd, STDERR_FILENO);
    close(out_fd);
  }

  if (!cwd.empty()) {
    std::error_code ec;
    fs::current_path(cwd, ec);
  }

  std::vector<std::string> env_strings = prepare_env(env);
  std::vector<char*> envp;
  envp.reserve(env_strings.size() + 1);
  for (auto& s : env_strings) envp.push_back(s.data());
  envp.push_back(nullptr);

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  execvpe(argv[0], argv.data(), envp.data());
  _exit(127);
}

bool ProcessRunner::is_alive(int pid) {
  if (pid <= 0) return false;
  return kill(pid, 0) == 0;
}

bool ProcessRunner::kill_pid(int pid, int sig, int timeout_ms) {
  if (!is_alive(pid)) return true;
  kill(pid, sig);

  auto start = std::chrono::steady_clock::now();
  while (is_alive(pid)) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() >= timeout_ms) {
      kill(pid, SIGKILL);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      return !is_alive(pid);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

bool ProcessRunner::kill_pgid(int pgid, int sig, int timeout_ms) {
  if (pgid <= 1) return true;
  kill(-pgid, sig);

  auto start = std::chrono::steady_clock::now();
  while (kill(-pgid, 0) == 0) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() >= timeout_ms) {
      kill(-pgid, SIGKILL);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      return kill(-pgid, 0) != 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

std::string ProcessRunner::read_cmdline(int pid) {
  if (pid <= 0) return {};
  std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline");
  if (!f) return {};
  std::string cmd;
  char c;
  while (f.get(c)) {
    if (c == '\0') cmd += ' ';
    else cmd += c;
  }
  while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
  return cmd;
}

}  // namespace paperboat::core
