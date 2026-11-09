#pragma once

// What the tests need from the operating system, spelled once for POSIX and once for Windows: the
// environment, a shell command line, quoting for std::system, our pid, waiting on a detached process,
// and a path written into TOML or JSON text.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/platform.hpp"

namespace test_platform {

inline void set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name.c_str(), value.c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

inline void unset_env(const std::string& name) {
#ifdef _WIN32
  _putenv_s(name.c_str(), "");  // an empty value removes the variable
#else
  unsetenv(name.c_str());
#endif
}

inline int current_pid() { return paperboat::platform::current_pid(); }

// A command line for the platform's shell: sh -c on POSIX, cmd /c on Windows.
inline std::vector<std::string> sh(const std::string& posix, const std::string& windows) {
#ifdef _WIN32
  (void)posix;
  return {"cmd", "/c", windows};
#else
  (void)windows;
  return {"sh", "-c", posix};
#endif
}

// A process that stays alive for about `seconds`.
inline std::vector<std::string> sleeper(int seconds) {
  return sh("sleep " + std::to_string(seconds), "ping -n " + std::to_string(seconds + 1) + " 127.0.0.1 >nul");
}

// One argument quoted for the shell std::system runs: sh (single quotes) or cmd.exe (double quotes).
inline std::string quoted(const std::string& arg) {
#ifdef _WIN32
  return "\"" + arg + "\"";
#else
  std::string out = "'";
  for (char c : arg) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
#endif
}

// A path inside TOML or JSON text a test writes: forward slashes, which both read back as the same path,
// where a Windows backslash would start an escape ("C:\Users" is a bad \U escape in TOML).
inline std::string text_path(const std::filesystem::path& p) { return p.generic_string(); }

inline bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit = std::chrono::seconds(5)) {
  const auto start = std::chrono::steady_clock::now();
  while (!done()) {
    if (std::chrono::steady_clock::now() - start > limit) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

// Waits for a process run_detached started to finish. POSIX: it is our child, so reap it (a zombie
// would still look alive). Windows: a finished process is gone.
inline bool wait_exit(int pid) {
#ifdef _WIN32
  return wait_for([&] { return !paperboat::core::ProcessRunner::is_alive(pid); }, std::chrono::seconds(10));
#else
  int status = 0;
  return waitpid(pid, &status, 0) == pid;
#endif
}

}  // namespace test_platform
