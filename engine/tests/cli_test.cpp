#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "../tools/paperboat_tui.hpp"

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
    auto res = core::BackendManager::check_backend(ex_dir / "cart-pole-cpp", /*fast=*/true);
    CHECK(res.ok);
    CHECK(res.domain == "cart-pole");
    CHECK(res.manifest_ok);
    CHECK(res.catalog_ok);
  }
}

TEST_CASE("BackendManifest load_manifest validates schema and required fields") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_manifest_val";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  // 1. Missing backend.toml
  std::string err;
  auto m_missing = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK_FALSE(m_missing.has_manifest);
  CHECK_THAT(err, ContainsSubstring("backend.toml missing"));

  // 2. Unsupported format version (e.g. format = 99)
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 99\nname = \"test\"\nlanguage = \"python\"\nrun = \"server.py\"\n";
  }
  err.clear();
  auto m_bad_ver = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK_FALSE(m_bad_ver.has_manifest);
  CHECK_THAT(err, ContainsSubstring("unsupported backend.toml format version 99"));

  // 3. Missing name
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 0\nlanguage = \"python\"\nrun = \"server.py\"\n";
  }
  err.clear();
  auto m_no_name = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK_FALSE(m_no_name.has_manifest);
  CHECK_THAT(err, ContainsSubstring("missing required field 'name'"));

  // 4. Valid manifest
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 0\nname = \"my-service\"\nversion = \"1.2.3\"\nlanguage = \"cpp\"\nrun = \"build/my-bin\"\nport = 9000\n"
        << "toolkits = [\"opencv\"]\n[test]\ngolden = \"expected.json\"\n";
  }
  err.clear();
  auto m_valid = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK(m_valid.has_manifest);
  CHECK(err.empty());
  CHECK(m_valid.format == 0);
  CHECK(m_valid.name == "my-service");
  CHECK(m_valid.version == "1.2.3");
  CHECK(m_valid.language == "cpp");
  CHECK(m_valid.run == "build/my-bin");
  CHECK(m_valid.port == 9000);
  CHECK(m_valid.descriptor == "descriptor.toml");
  CHECK(m_valid.toolkits == std::vector<std::string>{"opencv"});
  CHECK(m_valid.golden == "expected.json");

  fs::remove_all(tmp_dir, ec);
}

TEST_CASE("BackendManager check_backend handles broken descriptor with line accuracy") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_broken_desc";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  {
    std::ofstream btoml(tmp_dir / "backend.toml");
    btoml << "[backend]\nformat = 0\nname = \"broken\"\nlanguage = \"python\"\nrun = \"server.py\"\n";
  }
  {
    std::ofstream srv(tmp_dir / "server.py");
    srv << "# dummy\n";
  }
  {
    std::ofstream desc(tmp_dir / "descriptor.toml");
    desc << "domain = \"broken\"\nthis is invalid syntax !!!\n";
  }

  auto res = core::BackendManager::check_backend(tmp_dir, /*fast=*/true);
  CHECK_FALSE(res.ok);
  CHECK_FALSE(res.manifest_ok);
  REQUIRE_FALSE(res.errors.empty());
  CHECK_THAT(res.errors.front(), ContainsSubstring("descriptor syntax error"));
  CHECK_THAT(res.errors.front(), ContainsSubstring(":2:"));

  fs::remove_all(tmp_dir, ec);
}

TEST_CASE("BackendManager check_backend detects missing block in descriptor") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_missing_block";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  {
    std::ofstream btoml(tmp_dir / "backend.toml");
    btoml << "[backend]\nformat = 0\nname = \"missing-block\"\nlanguage = \"python\"\nrun = \"server.py\"\n";
  }
  {
    std::ofstream srv(tmp_dir / "server.py");
    srv << "# dummy\n";
  }
  {
    std::ofstream desc(tmp_dir / "descriptor.toml");
    desc << "domain = \"missing-block\"\nimport = [\"math\"]\n[[blocks]]\nuse = \"math/totally_nonexistent_xyz\"\n";
  }

  auto res = core::BackendManager::check_backend(tmp_dir, /*fast=*/true);
  CHECK_FALSE(res.ok);
  CHECK(res.manifest_ok);
  CHECK_FALSE(res.catalog_ok);
  REQUIRE_FALSE(res.errors.empty());
  CHECK_THAT(res.errors.front(), ContainsSubstring("no template \"math/totally_nonexistent_xyz\""));

  fs::remove_all(tmp_dir, ec);
}

TEST_CASE("BackendManager pack_backend and unpack_backend roundtrip excludes build junk") {
  auto ex_dir = core::BackendManager::find_examples_dir();
  if (ex_dir.empty() || !fs::is_directory(ex_dir / "cart-pole-cpp")) {
    return;
  }

  const fs::path src_dir = ex_dir / "cart-pole-cpp";
  const fs::path pkg_path = fs::temp_directory_path() / "test_cart_pole_roundtrip.paperboat-pkg";
  const fs::path unpack_dir = fs::temp_directory_path() / "test_cart_pole_unpacked";

  std::error_code ec;
  fs::remove(pkg_path, ec);
  fs::remove_all(unpack_dir, ec);

  // Pack backend
  auto pack_res = core::BackendManager::pack_backend(src_dir, pkg_path, /*fast=*/true);
  REQUIRE(pack_res.ok);
  CHECK(pack_res.file_count >= 6);
  CHECK(fs::is_regular_file(pkg_path));

  // Unpack backend and verify checksums
  auto unpack_res = core::BackendManager::unpack_backend(pkg_path, unpack_dir, /*verify=*/true);
  REQUIRE(unpack_res.ok);
  CHECK(unpack_res.file_count == pack_res.file_count);
  CHECK(fs::is_regular_file(unpack_dir / "backend.toml"));
  CHECK(fs::is_regular_file(unpack_dir / "descriptor.toml"));
  CHECK_FALSE(fs::exists(unpack_dir / "build"));

  // Check the unpacked directory passes verification
  auto check_unpacked = core::BackendManager::check_backend(unpack_dir, /*fast=*/true);
  CHECK(check_unpacked.ok);
  CHECK(check_unpacked.name == "cart-pole");

  // Clean up
  fs::remove(pkg_path, ec);
  fs::remove_all(unpack_dir, ec);
}

TEST_CASE("WaveSpinner starts and stops cleanly") {
  paperboat::tui::WaveSpinner spinner("Building something...");
  spinner.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  spinner.set_message("Still building...");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  spinner.stop(true, "Build succeeded");
  CHECK(true);
}

TEST_CASE("render_banner non-interactive executes cleanly without exception") {
  CHECK_NOTHROW(paperboat::tui::render_banner(false));
}
