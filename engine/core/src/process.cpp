#include "paperboat/core/process.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
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

using Env = std::vector<std::pair<std::string, std::string>>;

// Build environment vector for execvpe / execve: `custom_env` first, then every inherited
// variable it doesn't override.
std::vector<std::string> prepare_env(const Env& custom_env) {
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
      if (eq != std::string::npos && std::ranges::find(overridden, entry.substr(0, eq)) == overridden.end()) {
        env_strings.push_back(entry);
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

// In a forked child: move to `cwd`, then replace the process with `args`. Never returns.
[[noreturn]] void exec_child(const std::vector<std::string>& args, const fs::path& cwd, const Env& env) {
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

// The code a waitpid status reports: the exit status, or 128 + the signal; else `fallback`.
int exit_code_of(int status, int fallback) {
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return fallback;
}

struct Pipes {
  int out = -1;
  int err = -1;
};

// Fork `args` in its own process group with stdout and stderr on pipes. Returns the child
// pid and the read ends (non-blocking), or -1.
pid_t spawn_piped(const std::vector<std::string>& args, const fs::path& cwd, const Env& env, Pipes& pipes) {
  int pipe_out[2];
  int pipe_err[2];
  if (pipe(pipe_out) != 0 || pipe(pipe_err) != 0) return -1;

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
    exec_child(args, cwd, env);
  }

  close(pipe_out[1]);
  close(pipe_err[1]);
  fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
  fcntl(pipe_err[0], F_SETFL, O_NONBLOCK);
  pipes = {.out = pipe_out[0], .err = pipe_err[0]};
  return pid;
}

// Read both pipes until both close. Each round: `before()` (false stops), a poll of up to
// 100 ms, each chunk read to `on_data` (stream 0 stdout, 1 stderr), then `after()` (false
// stops). Closes the pipes.
void pump(Pipes pipes,
          const std::function<bool()>& before,
          const std::function<void(int, std::string_view)>& on_data,
          const std::function<bool()>& after) {
  std::array<pollfd, 2> fds{};
  fds[0].fd = pipes.out;
  fds[0].events = POLLIN;
  fds[1].fd = pipes.err;
  fds[1].events = POLLIN;
  const std::array<int, 2> owned{pipes.out, pipes.err};
  std::array<bool, 2> closed{false, false};

  std::array<char, 4096> buf{};
  while (!closed[0] || !closed[1]) {
    if (!before()) break;

    int ret = poll(fds.data(), 2, 100);
    if (ret < 0 && errno == EINTR) continue;

    if (ret > 0) {
      for (int i = 0; i < 2; ++i) {
        const auto at = static_cast<std::size_t>(i);
        if (!closed[at] && (fds[at].revents & POLLIN)) {
          ssize_t n = read(owned[at], buf.data(), buf.size());
          if (n > 0) on_data(i, {buf.data(), static_cast<std::size_t>(n)});
        }
        if (!closed[at] && (fds[at].revents & (POLLHUP | POLLERR))) {
          ssize_t n = 0;
          while ((n = read(owned[at], buf.data(), buf.size())) > 0) {
            on_data(i, {buf.data(), static_cast<std::size_t>(n)});
          }
          close(owned[at]);
          closed[at] = true;
          fds[at].fd = -1;
        }
      }
    }

    if (!after()) break;
  }

  for (std::size_t i = 0; i < 2; ++i) {
    if (!closed[i]) close(owned[i]);
  }
}

// Wait up to `timeout_ms` for `alive()` to turn false; past it, `signal(SIGKILL)`.
bool wait_or_kill(const std::function<bool()>& alive, const std::function<void(int)>& signal, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  while (alive()) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() >= timeout_ms) {
      signal(SIGKILL);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      return !alive();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

}  // namespace

ProcessResult ProcessRunner::run_sync(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const Env& env,
    std::chrono::milliseconds timeout) {
  ProcessResult result;
  if (args.empty()) return result;

  Pipes pipes;
  pid_t pid = spawn_piped(args, cwd, env, pipes);
  if (pid < 0) {
    result.exit_code = -1;
    return result;
  }

  auto start_time = std::chrono::steady_clock::now();
  pump(
      pipes,
      [&] {
        if (timeout.count() > 0) {
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start_time);
          if (elapsed >= timeout) {
            result.timed_out = true;
            kill_pgid(pid, SIGKILL, 500);
            return false;
          }
        }
        return true;
      },
      [&](int stream, std::string_view chunk) { (stream == 0 ? result.stdout_text : result.stderr_text).append(chunk); },
      [&] {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) result.exit_code = exit_code_of(status, result.exit_code);
        return true;
      });

  int status = 0;
  if (waitpid(pid, &status, 0) == pid) result.exit_code = exit_code_of(status, result.exit_code);
  return result;
}

int ProcessRunner::run_stream(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const Env& env,
    ProcessOutputCallback on_output,
    std::stop_token stop) {
  if (args.empty()) return -1;

  Pipes pipes;
  pid_t pid = spawn_piped(args, cwd, env, pipes);
  if (pid < 0) return -1;

  g_active_child_pgid.store(pid);
  struct sigaction sa_int{}, sa_term{}, old_int{}, old_term{};
  sa_int.sa_handler = signal_handler;
  sigemptyset(&sa_int.sa_mask);
  sa_term.sa_handler = signal_handler;
  sigemptyset(&sa_term.sa_mask);
  sigaction(SIGINT, &sa_int, &old_int);
  sigaction(SIGTERM, &sa_term, &old_term);

  std::array<std::string, 2> line_bufs;  // stdout, stderr: the part after the last newline
  constexpr std::array<std::string_view, 2> names{"stdout", "stderr"};
  pump(
      pipes,
      [&] {
        if (stop.stop_requested()) {
          kill_pgid(pid, SIGTERM, 1000);
          return false;
        }
        return true;
      },
      [&](int stream, std::string_view chunk) {
        const auto at = static_cast<std::size_t>(stream);
        line_bufs[at].append(chunk);
        process_buffer_lines(line_bufs[at], names[at], on_output);
      },
      [&] {
        int status = 0;
        return waitpid(pid, &status, WNOHANG) != pid;
      });

  // Drain remaining buffers
  for (std::size_t i = 0; i < 2; ++i) {
    if (!line_bufs[i].empty() && on_output) on_output(names[i], trim_trailing_newlines(line_bufs[i]));
  }

  // Clean termination of process group
  kill_pgid(pid, SIGTERM, 1000);

  int exit_code = -1;
  int status = 0;
  if (waitpid(pid, &status, 0) == pid) exit_code = exit_code_of(status, exit_code);

  g_active_child_pgid.store(0);
  sigaction(SIGINT, &old_int, nullptr);
  sigaction(SIGTERM, &old_term, nullptr);

  return exit_code;
}

int ProcessRunner::run_detached(
    const std::vector<std::string>& args,
    const fs::path& cwd,
    const Env& env,
    const fs::path& log_file) {
  if (args.empty()) return -1;

  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid > 0) {
    // Parent returns child pid
    return pid;
  }

  // Child: a session of its own, detached from the terminal
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

  exec_child(args, cwd, env);
}

bool ProcessRunner::is_alive(int pid) {
  if (pid <= 0) return false;
  return kill(pid, 0) == 0;
}

bool ProcessRunner::kill_pid(int pid, int sig, int timeout_ms) {
  if (!is_alive(pid)) return true;
  kill(pid, sig);
  return wait_or_kill([pid] { return is_alive(pid); }, [pid](int s) { kill(pid, s); }, timeout_ms);
}

bool ProcessRunner::kill_pgid(int pgid, int sig, int timeout_ms) {
  if (pgid <= 1) return true;
  kill(-pgid, sig);
  return wait_or_kill([pgid] { return kill(-pgid, 0) == 0; }, [pgid](int s) { kill(-pgid, s); }, timeout_ms);
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
