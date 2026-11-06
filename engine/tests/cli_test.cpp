#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"

using Catch::Matchers::ContainsSubstring;
namespace core = paperboat::core;
namespace fs = std::filesystem;

TEST_CASE("ProcessRunner sync execution captures exit code and output") {
  auto res = core::ProcessRunner::run_sync({"echo", "hello", "paperboat"});
  CHECK(res.ok());
  CHECK(res.exit_code == 0);
  CHECK_THAT(res.stdout_text, ContainsSubstring("hello paperboat"));

  auto err_res = core::ProcessRunner::run_sync({"sh", "-c", "echo 'failed' >&2; exit 42"});
  CHECK_FALSE(err_res.ok());
  CHECK(err_res.exit_code == 42);
  CHECK_THAT(err_res.stderr_text, ContainsSubstring("failed"));
}

TEST_CASE("ProcessRunner sync timeout kills hanging command") {
  auto start = std::chrono::steady_clock::now();
  auto res = core::ProcessRunner::run_sync({"sleep", "10"}, {}, {}, std::chrono::milliseconds(150));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  CHECK(res.timed_out);
  CHECK(elapsed.count() < 2000);
}

TEST_CASE("ProcessRunner is_alive correctly identifies active and dead pids") {
  CHECK(core::ProcessRunner::is_alive(getpid()));
  CHECK_FALSE(core::ProcessRunner::is_alive(9999999));
}

TEST_CASE("BackendManager port inspection and registry") {
  // Random high port should not be listening
  CHECK_FALSE(core::BackendManager::is_port_listening(59483));

  // Runtime registry paths
  CHECK(core::BackendManager::runtime_dir() == fs::path("/tmp/paperboat-runtime"));
  CHECK(core::BackendManager::runtime_record_path(8765) == fs::path("/tmp/paperboat-runtime/backend-8765.json"));

  // Register a mock running backend
  core::RunningBackend mock;
  mock.port = 59483;
  mock.pid = getpid();
  mock.engine = "test-engine";
  mock.domain = "test-domain";
  mock.cwd = fs::current_path();
  mock.executable = "test-bin";
  mock.ws_url = "ws://localhost:59483";
  mock.started_at = 123456789;

  core::BackendManager::register_running_backend(mock);
  const auto rec_path = core::BackendManager::runtime_record_path(59483);
  CHECK(fs::is_regular_file(rec_path));

  // Unregister cleans up
  core::BackendManager::unregister_running_backend(59483);
  CHECK_FALSE(fs::exists(rec_path));
}

TEST_CASE("BackendManager check_backend validates missing vs valid descriptors") {
  auto non_existent = core::BackendManager::check_backend("/non/existent/path");
  CHECK_FALSE(non_existent.ok);
  CHECK_FALSE(non_existent.errors.empty());

  // Check example backends if available
  auto ex_dir = core::BackendManager::find_examples_dir();
  if (!ex_dir.empty() && fs::is_directory(ex_dir / "cart-pole-cpp")) {
    auto res = core::BackendManager::check_backend(ex_dir / "cart-pole-cpp");
    CHECK(res.ok);
    CHECK(res.domain == "cart-pole");
  }
}
