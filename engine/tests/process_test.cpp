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
#include "test_platform.hpp"

using paperboat::core::ProcessRunner;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using test_platform::sh;
using test_platform::sleeper;
using test_platform::wait_for;

namespace {

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

TEST_CASE("process: is_alive is false for nothing") {
  CHECK_FALSE(ProcessRunner::is_alive(0));
  CHECK_FALSE(ProcessRunner::is_alive(-5));
}
