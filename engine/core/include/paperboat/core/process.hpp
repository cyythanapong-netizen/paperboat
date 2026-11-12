#pragma once

#include <chrono>
#include <csignal>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <stop_token>
#include <utility>
#include <vector>

#ifndef SIGKILL  // Windows has no SIGKILL; the number keeps the API the same
#define SIGKILL 9
#endif

namespace paperboat::core {

struct ProcessResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  bool timed_out = false;

  bool ok() const { return exit_code == 0 && !timed_out; }
};

using ProcessOutputCallback = std::function<void(std::string_view stream, std::string_view line)>;

class ProcessRunner {
 public:
  // Synchronous execution: waits for exit, captures stdout and stderr text.
  static ProcessResult run_sync(
      const std::vector<std::string>& args,
      const std::filesystem::path& cwd = {},
      const std::vector<std::pair<std::string, std::string>>& env = {},
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

  // Streaming execution: forwards stdout and stderr lines to callback in real-time.
  // Handles process group isolation and ensures children are cleanly reaped on stop or signal.
  static int run_stream(
      const std::vector<std::string>& args,
      const std::filesystem::path& cwd = {},
      const std::vector<std::pair<std::string, std::string>>& env = {},
      ProcessOutputCallback on_output = nullptr,
      std::stop_token stop = {});

  // Detached daemon execution: runs in a new session (setsid), redirecting output to log_file.
  // Returns the spawned child PID, or -1 on error.
  static int run_detached(
      const std::vector<std::string>& args,
      const std::filesystem::path& cwd = {},
      const std::vector<std::pair<std::string, std::string>>& env = {},
      const std::filesystem::path& log_file = {});

  // Check if a process PID is currently alive.
  static bool is_alive(int pid);

  // Terminate a process: sends SIGTERM, waits up to timeout_ms, escalates to SIGKILL if still alive.
  static bool kill_pid(int pid, int sig = 15 /*SIGTERM*/, int timeout_ms = 1500);

  // Terminate an entire process group (kill -pgid).
  static bool kill_pgid(int pgid, int sig = 15 /*SIGTERM*/, int timeout_ms = 1500);

  // Read process command line from /proc/<pid>/cmdline.
  static std::string read_cmdline(int pid);
};

#ifdef _WIN32
// One argument quoted so the C runtime (CommandLineToArgvW) reads it back unchanged. For the CRT's
// _spawn*/_exec*, which join their arguments with spaces and quote nothing: a path with a space
// ("C:\Users\Jane Doe\...") arrives as two arguments otherwise.
std::string quote_command_arg(const std::string& arg);
#endif

}  // namespace paperboat::core
