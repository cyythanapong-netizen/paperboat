// ProcessRunner on every platform: the same checks run on POSIX (process.cpp) and Windows (process_win.cpp).
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "paperboat/core/process.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW
#pragma comment(lib, "shell32.lib")
#endif

using paperboat::core::ProcessRunner;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// A command line for the platform's shell.
std::vector<std::string> sh(const std::string& posix, const std::string& windows) {
#ifdef _WIN32
  (void)posix;
  return {"cmd", "/c", windows};
#else
  (void)windows;
  return {"sh", "-c", posix};
#endif
}

// A process that stays alive for about `seconds`.
std::vector<std::string> sleeper(int seconds) {
  return sh("sleep " + std::to_string(seconds), "ping -n " + std::to_string(seconds + 1) + " 127.0.0.1 >nul");
}

bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit = 5s) {
  const auto start = std::chrono::steady_clock::now();
  while (!done()) {
    if (std::chrono::steady_clock::now() - start > limit) return false;
    std::this_thread::sleep_for(20ms);
  }
  return true;
}

// A long-lived process that is not our child, as a backend is not the child of the `paperboat stop` that ends it
// (a zombie child would still look alive). `own_group`: it leads a process group of its own. Returns its pid.
int daemon_sleeper(bool own_group) {
#ifdef _WIN32
  (void)own_group;
  return ProcessRunner::run_detached(sleeper(30));  // no zombies here: a finished process is gone
#else
  // macOS has no setsid command; there `set -m` (job control) gives the background job a
  // process group of its own, as setsid does
  const std::string job = "sleep 30 >/dev/null 2>&1 &";
  const auto r = ProcessRunner::run_sync(
      {"sh", "-c",
       own_group ? "if command -v setsid >/dev/null 2>&1; then setsid " + job + " else set -m; " + job + " fi; echo $!"
                 : job + " echo $!"});
  const int pid = std::atoi(r.stdout_text.c_str());
  if (own_group) wait_for([&] { return kill(-pid, 0) == 0; });  // setsid runs a moment after the shell has exited
  return pid;
#endif
}

std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

}  // namespace

TEST_CASE("process: run_sync captures stdout, stderr and the exit code") {
  const auto r = ProcessRunner::run_sync(sh("echo out; echo err >&2; exit 3", "echo out& echo err 1>&2& exit /b 3"));
  CHECK(trimmed(r.stdout_text) == "out");
  CHECK(trimmed(r.stderr_text) == "err");
  CHECK(r.exit_code == 3);
  CHECK_FALSE(r.timed_out);
}

TEST_CASE("process: a missing program is a failure, not a crash") {
  const auto r = ProcessRunner::run_sync({"paperboat-no-such-program-xyz"});
  CHECK_FALSE(r.ok());
}

TEST_CASE("process: run_sync passes the working directory and the environment") {
  const fs::path dir = fs::temp_directory_path() / "pb_process_cwd";
  fs::create_directories(dir);
  const auto cwd = ProcessRunner::run_sync(sh("pwd", "cd"), dir);
  CHECK(fs::equivalent(fs::path(trimmed(cwd.stdout_text)), dir));
  const auto env = ProcessRunner::run_sync(sh("echo $PB_PROCESS_TEST", "echo %PB_PROCESS_TEST%"), {}, {{"PB_PROCESS_TEST", "yes"}});
  CHECK(trimmed(env.stdout_text) == "yes");
  fs::remove_all(dir);
}

TEST_CASE("process: run_sync stops a command that runs past its timeout") {
  const auto start = std::chrono::steady_clock::now();
  const auto r = ProcessRunner::run_sync(sleeper(20), {}, {}, 300ms);
  CHECK(r.timed_out);
  CHECK(std::chrono::steady_clock::now() - start < 10s);
}

TEST_CASE("process: run_stream hands over output a line at a time and returns the exit code") {
  std::vector<std::string> lines;
  const int code = ProcessRunner::run_stream(sh("echo one; echo two; exit 4", "echo one& echo two& exit /b 4"), {}, {},
                                             [&](std::string_view stream, std::string_view line) {
                                               if (stream == "stdout") lines.emplace_back(line);
                                             });
  CHECK(code == 4);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "one");
  CHECK(lines[1] == "two");
}

TEST_CASE("process: run_stream ends its child when asked to stop") {
  std::stop_source stop;
  std::jthread asker([&] {
    std::this_thread::sleep_for(300ms);
    stop.request_stop();
  });
  const auto start = std::chrono::steady_clock::now();
  ProcessRunner::run_stream(sleeper(20), {}, {}, nullptr, stop.get_token());
  CHECK(std::chrono::steady_clock::now() - start < 10s);
}

TEST_CASE("process: run_detached logs to a file, and a detached process can be found and killed") {
  const fs::path log = fs::temp_directory_path() / "pb_process_detached.log";
  fs::remove(log);
  const int quick = ProcessRunner::run_detached(sh("echo detached", "echo detached"), {}, {}, log);
  REQUIRE(quick > 0);
  CHECK(wait_for([&] {
    std::ifstream in(log);
    std::stringstream text;
    text << in.rdbuf();
    return text.str().find("detached") != std::string::npos;
  }));

  const int pid = daemon_sleeper(false);
  REQUIRE(pid > 0);
  CHECK(ProcessRunner::is_alive(pid));
  CHECK(ProcessRunner::kill_pid(pid, 15, 3000));
  CHECK(wait_for([&] { return !ProcessRunner::is_alive(pid); }));

  const int group = daemon_sleeper(true);
  REQUIRE(group > 0);
  CHECK(ProcessRunner::kill_pgid(group, 15, 3000));
  CHECK(wait_for([&] { return !ProcessRunner::is_alive(group); }));
  fs::remove(log);
}

#ifdef _WIN32
// `paperboat start --daemon` in a CI step: the step's output is an inheritable pipe, and a detached server that
// inherited it kept it open, so the step never saw the end of its output. A child gets its three handles only.
TEST_CASE("process: a detached process does not inherit our other handles") {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE read_end = nullptr, write_end = nullptr;
  REQUIRE(CreatePipe(&read_end, &write_end, &sa, 0));  // both ends inheritable, like a redirected stdout

  const int pid = daemon_sleeper(false);
  REQUIRE(pid > 0);
  CloseHandle(write_end);
  // With no writer left the pipe reports broken; a child holding a copy would keep it open for 30 s.
  CHECK(wait_for([&] {
    DWORD avail = 0;
    return !PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) && GetLastError() == ERROR_BROKEN_PIPE;
  }));
  CloseHandle(read_end);
  CHECK(ProcessRunner::kill_pid(pid, 15, 3000));
}

// The console launch (_spawnvp "node <main.js>") under C:\Users\<name with a space>: node got the path cut
// at the space. Each argument, quoted and joined, must read back as itself.
TEST_CASE("process: quote_command_arg survives the Windows command-line parser") {
  const std::vector<std::string> args = {
      "node", R"(C:\Users\Jane Doe\paperboat\apps\cli\dist\main.js)", "plain", "", R"(ends in a backslash\)",
      R"(say "hi")", R"(C:\dir with space\\)", "tab\there"};
  std::string line;
  for (const auto& a : args) line += (line.empty() ? "" : " ") + paperboat::core::quote_command_arg(a);
  const int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide.data(), n);
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(wide.c_str(), &argc);
  REQUIRE(argv);
  REQUIRE(argc == static_cast<int>(args.size()));
  for (int i = 0; i < argc; ++i) {
    const std::wstring expected(args[static_cast<std::size_t>(i)].begin(), args[static_cast<std::size_t>(i)].end());
    CHECK(std::wstring(argv[i]) == expected);
  }
  LocalFree(argv);
}

// `paperboat start squero-studio-mx` launches bin/squero-mx, a `#!/usr/bin/env node` script, and
// squero-om's bin/squero-om is bash: CreateProcess runs neither, so the #! line names the program.
TEST_CASE("process: a #! script runs through its interpreter") {
  const fs::path dir = fs::temp_directory_path() / "pb process shebang";  // a space, as under C:\Users\Jane Doe
  fs::create_directories(dir);
  const fs::path node_script = dir / "hello-node";
  std::ofstream(node_script, std::ios::binary) << "#!/usr/bin/env node\nconsole.log('node ' + process.argv[2]);\n";
  const fs::path sh_script = dir / "hello-sh";
  std::ofstream(sh_script, std::ios::binary) << "#!/bin/sh\necho \"sh $1\"\n";

  const auto node = ProcessRunner::run_sync({node_script.string(), "arg one"});
  CHECK(node.exit_code == 0);
  CHECK(trimmed(node.stdout_text) == "node arg one");
  const auto sh = ProcessRunner::run_sync({sh_script.string(), "arg two"});
  CHECK(sh.exit_code == 0);
  CHECK(trimmed(sh.stdout_text) == "sh arg two");
  fs::remove_all(dir);
}
#endif

TEST_CASE("process: is_alive is false for nothing") {
  CHECK_FALSE(ProcessRunner::is_alive(0));
  CHECK_FALSE(ProcessRunner::is_alive(-5));
}
