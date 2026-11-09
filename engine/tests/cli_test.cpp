#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/installer.hpp"
#include "paperboat/toolkit/platform.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#include "../tools/paperboat_tui.hpp"
#include <nlohmann/json.hpp>

using Catch::Matchers::ContainsSubstring;
namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
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

TEST_CASE("ProcessRunner stream and detached runs pass cwd, env and output through") {
  const fs::path dir = fs::temp_directory_path() / "pb_process_test";
  fs::create_directories(dir);
  const std::vector<std::pair<std::string, std::string>> env{{"PB_PROBE", "on"}};

  // Lines per stream: CRLF trimmed, a tail with no newline still delivered.
  std::map<std::string, std::vector<std::string>> lines;
  core::ProcessRunner::run_stream(
      {"sh", "-c", "printf 'a\\r\\nb\\n'; printf 'e1\\n' >&2; printf \"$PB_PROBE $(pwd)\""}, dir, env,
      [&](std::string_view stream, std::string_view line) { lines[std::string(stream)].emplace_back(line); });
  CHECK(lines["stdout"] == std::vector<std::string>{"a", "b", "on " + fs::canonical(dir).string()});
  CHECK(lines["stderr"] == std::vector<std::string>{"e1"});

  // The exit code survives the pump reaping the child.
  CHECK(core::ProcessRunner::run_stream({"sh", "-c", "exit 0"}) == 0);
  CHECK(core::ProcessRunner::run_stream({"sh", "-c", "exit 3"}) == 3);

  const fs::path log = dir / "detached.log";
  fs::remove(log);
  const int pid = core::ProcessRunner::run_detached({"sh", "-c", "echo \"$PB_PROBE $(pwd)\"; echo err >&2"}, dir, env, log);
  REQUIRE(pid > 0);
  int status = 0;
  waitpid(pid, &status, 0);
  std::ifstream in(log);
  std::string a, b;
  std::getline(in, a);
  std::getline(in, b);
  CHECK(a == "on " + fs::canonical(dir).string());
  CHECK(b == "err");
  fs::remove_all(dir);
}

TEST_CASE("ProcessRunner is_alive correctly identifies active and dead pids") {
  CHECK(core::ProcessRunner::is_alive(getpid()));
  CHECK_FALSE(core::ProcessRunner::is_alive(9999999));
}

TEST_CASE("BackendManager finds the process listening on a port, and its command line") {
  // a listening socket of our own, on a port the kernel picks
  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(sock >= 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  REQUIRE(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  REQUIRE(listen(sock, 1) == 0);
  socklen_t len = sizeof(addr);
  REQUIRE(getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
  const int port = ntohs(addr.sin_port);

  CHECK(core::BackendManager::is_port_listening(port));
  CHECK(core::BackendManager::find_pid_by_port(port) == getpid());
  CHECK_THAT(core::ProcessRunner::read_cmdline(getpid()), ContainsSubstring("paperboat_tests"));
  close(sock);
  CHECK(core::BackendManager::find_pid_by_port(port) == 0);
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

TEST_CASE("BackendManifest load_manifest validates solver setting in backend.toml") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_manifest_solver";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  // 1. Valid solver in [backend]
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 0\nname = \"test-solver\"\nlanguage = \"python\"\nrun = \"server.py\"\nsolver = \"rk45\"\n";
  }
  std::string err;
  auto m_valid = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK(m_valid.has_manifest);
  CHECK(err.empty());
  CHECK(m_valid.solver == "rk45");

  // 2. Valid solver in [run]
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 0\nname = \"test-solver-run\"\nlanguage = \"python\"\nrun = \"server.py\"\n\n[run]\nsolver = \"discrete\"\n";
  }
  err.clear();
  auto m_run = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK(m_run.has_manifest);
  CHECK(err.empty());
  CHECK(m_run.solver == "discrete");

  // 3. Invalid solver name fails
  {
    std::ofstream out(tmp_dir / "backend.toml");
    out << "[backend]\nformat = 0\nname = \"bad-solver\"\nlanguage = \"python\"\nrun = \"server.py\"\nsolver = \"nonexistent_solver\"\n";
  }
  err.clear();
  auto m_bad = core::BackendManager::load_manifest(tmp_dir, &err);
  CHECK_FALSE(m_bad.has_manifest);
  CHECK_THAT(err, ContainsSubstring("invalid solver 'nonexistent_solver'"));

  fs::remove_all(tmp_dir, ec);
}

TEST_CASE("BackendManager check_backend checks solver setting and solver api detection") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_check_solver";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  {
    std::ofstream btoml(tmp_dir / "backend.toml");
    btoml << "[backend]\nformat = 0\nname = \"solver-check\"\nlanguage = \"python\"\nrun = \"server.py\"\nsolver = \"rk45\"\n";
  }
  {
    std::ofstream srv(tmp_dir / "server.py");
    srv << "# dummy\n";
  }
  {
    std::ofstream desc(tmp_dir / "descriptor.toml");
    desc << "domain = \"solver-check\"\nimport = [\"control\"]\n";
  }

  auto res = core::BackendManager::check_backend(tmp_dir, /*fast=*/true);
  CHECK(res.ok);
  CHECK(res.manifest_ok);
  CHECK(res.catalog_ok);
  CHECK(res.solver == "rk45");
  REQUIRE(res.steps.size() >= 2);
  CHECK_THAT(res.steps[0].message, ContainsSubstring("solver rk45"));
  CHECK_THAT(res.steps[1].message, ContainsSubstring("solver rk45"));

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

TEST_CASE("BackendManager check_backend handles toolkit = ['__all__'] importing all built-in blocks") {
  const fs::path tmp_dir = fs::temp_directory_path() / "pb_test_all_toolkits";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  fs::create_directories(tmp_dir, ec);

  {
    std::ofstream btoml(tmp_dir / "backend.toml");
    btoml << "[backend]\nformat = 0\nname = \"all-toolkits\"\nlanguage = \"python\"\nrun = \"server.py\"\ntoolkit = [\"__all__\"]\n";
  }
  {
    std::ofstream srv(tmp_dir / "server.py");
    srv << "# dummy\n";
  }
  {
    std::ofstream desc(tmp_dir / "descriptor.toml");
    desc << "domain = \"all-toolkits\"\ntoolkit = [\"__all__\"]\n";
  }

  auto res = core::BackendManager::check_backend(tmp_dir, /*fast=*/true);
  CHECK(res.ok);
  CHECK(res.manifest_ok);
  CHECK(res.catalog_ok);
  CHECK(res.block_count > 20); // Imports all built-in packages

  fs::remove_all(tmp_dir, ec);
}

TEST_CASE("BackendManager pack_backend and unpack_backend roundtrip excludes build junk") {
  auto ex_dir = core::BackendManager::find_examples_dir();
  if (ex_dir.empty() || !fs::is_directory(ex_dir / "cart-pole-cpp")) {
    return;
  }

  const fs::path src_dir = ex_dir / "cart-pole-cpp";
  const fs::path pkg_path = fs::temp_directory_path() / "test_cart_pole_roundtrip.ppk";
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

TEST_CASE("check_backend reports each stage as it starts and ends") {
  const fs::path dir = fs::temp_directory_path() / "pb_check_progress";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::ofstream(dir / "backend.toml") << "[backend]\nformat = 0\nname = \"probe\"\nlanguage = \"python\"\n"
                                         "run = \"server.py\"\ndescriptor = \"descriptor.toml\"\n";
  std::ofstream(dir / "descriptor.toml") << "domain = \"probe\"\nimport = [\"math\"]\n";
  std::ofstream(dir / "server.py") << "\n";

  std::vector<std::string> events;
  const auto res = core::BackendManager::check_backend(dir, /*fast=*/true, [&](const core::CheckStep& s, bool done) {
    events.push_back(s.name + (done ? (s.skipped ? ":skipped" : ":done") : ":start"));
  });
  CHECK(res.ok);
  CHECK(events == std::vector<std::string>{"manifest:start", "manifest:done", "catalog:start", "catalog:done",
                                           "smoke:skipped", "golden:skipped"});
  fs::remove_all(dir);
}

TEST_CASE("resolve_backend_dir makes a relative backend path absolute") {
  const fs::path rel = "pb_rel_backend";
  fs::create_directories(rel);
  std::ofstream(rel / "server.py") << "\n";
  const auto dir = core::BackendManager::resolve_backend_dir(rel);
  CHECK(dir.is_absolute());
  CHECK(fs::equivalent(dir, rel));
  fs::remove_all(rel);
}

TEST_CASE("list_toolkits includes built-in toolkit and inspect_toolkit categorizes blocks and instruments") {
  const auto list = tk::list_toolkits();
  REQUIRE_FALSE(list.empty());
  CHECK(list.front().name == "builtin");
  CHECK(list.front().builtin);
  CHECK(list.front().installed);
  CHECK_FALSE(list.front().optional);

  const auto j = nlohmann::json::parse(tk::toolkits_json(list));
  REQUIRE(j.is_array());
  REQUIRE_FALSE(j.empty());
  CHECK(j[0]["name"] == "builtin");
  CHECK(j[0]["builtin"] == true);
  CHECK(j[0]["installed"] == true);

  // Built-in toolkit cannot be installed or uninstalled
  CHECK_FALSE(tk::install_toolkit("builtin").ok);
  CHECK_FALSE(tk::uninstall_toolkit("builtin").ok);

  // Inspecting "builtin" aggregates flow blocks and dashboard instruments
  const auto builtin_res = tk::inspect_toolkit("builtin");
  CHECK(builtin_res.found);
  CHECK(builtin_res.name == "builtin");
  CHECK(builtin_res.builtin);
  CHECK(builtin_res.installed);
  CHECK_FALSE(builtin_res.flow_blocks.empty());
  CHECK_FALSE(builtin_res.dashboard_instruments.empty());

  const auto builtin_json = nlohmann::json::parse(tk::toolkit_inspect_json(builtin_res));
  CHECK(builtin_json["name"] == "builtin");
  CHECK(builtin_json["builtin"] == true);
  CHECK(builtin_json["flow_blocks"].size() == builtin_res.flow_blocks.size());
  CHECK(builtin_json["dashboard_instruments"].size() == builtin_res.dashboard_instruments.size());

  const auto builtin_text = tk::toolkit_inspect_text(builtin_res);
  CHECK_THAT(builtin_text, ContainsSubstring("Toolkit: builtin (built-in)"));
  CHECK_THAT(builtin_text, ContainsSubstring("Flow blocks"));
  CHECK_THAT(builtin_text, ContainsSubstring("Dashboard instruments"));

  // Inspecting individual packages ("math", "dashboard")
  const auto math_res = tk::inspect_toolkit("math");
  CHECK(math_res.found);
  CHECK(math_res.builtin);
  CHECK_FALSE(math_res.flow_blocks.empty());
  CHECK(math_res.dashboard_instruments.empty());

  const auto dash_res = tk::inspect_toolkit("dashboard");
  CHECK(dash_res.found);
  CHECK(dash_res.builtin);
  CHECK_FALSE(dash_res.dashboard_instruments.empty());

#ifdef PAPERBOAT_HAS_OPENCV
  tk::Registry reg = tk::builtins();
  tk::opencv::register_opencv(reg);
  const auto cv_res = tk::inspect_toolkit("opencv", reg);
  CHECK(cv_res.found);
  CHECK_FALSE(cv_res.builtin);
  CHECK(cv_res.installed);
  CHECK_FALSE(cv_res.station_blocks.empty());
  CHECK_FALSE(cv_res.flow_blocks.empty());
  CHECK_FALSE(cv_res.dashboard_instruments.empty());
#endif

  // Unknown toolkit returns found = false
  const auto unknown_res = tk::inspect_toolkit("nonexistent_xyz");
  CHECK_FALSE(unknown_res.found);
}

TEST_CASE("paperboat open validates project file and backend") {
  fs::path cli = "bin/paperboat";
  if (!fs::is_regular_file(cli)) cli = "engine/build/dev/tools/paperboat";
  if (!fs::is_regular_file(cli)) cli = fs::current_path() / "engine" / "build" / "dev" / "tools" / "paperboat";
  if (!fs::is_regular_file(cli)) cli = fs::current_path().parent_path() / "tools" / "paperboat";
  REQUIRE(fs::is_regular_file(cli));

  // Missing file fails
  auto res1 = core::ProcessRunner::run_sync({cli.string(), "open", "nonexistent_file.ppj"});
  CHECK(res1.exit_code != 0);

  // Invalid JSON fails
  const fs::path tmp = fs::temp_directory_path() / "bad_project.ppj";
  std::ofstream(tmp) << "not json content";
  auto res2 = core::ProcessRunner::run_sync({cli.string(), "open", tmp.string()});
  CHECK(res2.exit_code != 0);
  fs::remove(tmp);

  // Missing backend in project fails
  std::ofstream(tmp) << "{\"paperboat\": 0, \"flow\": {}}";
  auto res3 = core::ProcessRunner::run_sync({cli.string(), "open", tmp.string()});
  CHECK(res3.exit_code != 0);

  // Unknown backend fails
  std::ofstream(tmp) << "{\"paperboat\": 0, \"backend\": \"nonexistent_backend_xyz\", \"flow\": {}}";
  auto res4 = core::ProcessRunner::run_sync({cli.string(), "open", tmp.string()});
  CHECK(res4.exit_code != 0);
  fs::remove(tmp);

  // Valid project with --run runs headlessly against a backend
  const fs::path backend_dir = fs::temp_directory_path() / "pb_test_backend";
  fs::remove_all(backend_dir);
  fs::create_directories(backend_dir);
  std::ofstream(backend_dir / "backend.toml") << "[backend]\nformat = 0\nname = \"test_probe\"\nlanguage = \"python\"\n"
                                                 "run = \"server.py\"\ndescriptor = \"descriptor.toml\"\n";
  std::ofstream(backend_dir / "descriptor.toml") << "domain = \"test_probe\"\nimport = [\"math\"]\n";
  std::ofstream(backend_dir / "server.py") << "import sys; sys.exit(0)\n";

  const fs::path valid_proj = fs::temp_directory_path() / "test_run.ppj";
  std::ofstream(valid_proj) << "{\"paperboat\": 0, \"backend\": \"" + backend_dir.string() + "\", \"flow\": {\"nodes\": [], \"edges\": []}}";
  auto res5 = core::ProcessRunner::run_sync({cli.string(), "open", valid_proj.string(), "--run", "--no-build"});
  CHECK(res5.exit_code == 0);
  fs::remove(valid_proj);
  fs::remove_all(backend_dir);
}

namespace {
// The CLI built with these tests: <build>/tests/paperboat_tests -> <build>/tools/paperboat.
fs::path find_cli() {
  const fs::path beside = paperboat::platform::exe_path().parent_path().parent_path() / "tools" / "paperboat";
  return fs::is_regular_file(beside) ? beside : fs::path{"bin/paperboat"};
}
}  // namespace

TEST_CASE("every command in the table is in the help menu and the prompt's help") {
  const fs::path cli = find_cli();
  REQUIRE(fs::is_regular_file(cli));
  const auto table = nlohmann::json::parse(core::ProcessRunner::run_sync({cli.string(), "commands", "--json"}).stdout_text);
  const auto help = core::ProcessRunner::run_sync({cli.string(), "--help"}).stdout_text;
  REQUIRE(table.size() > 10);
  for (const auto& c : table) {
    const std::string name = c["name"];
    INFO("command: " << name);
    CHECK(help.find("  " + name) != std::string::npos);  // a line of the manual starts with it
  }
}

TEST_CASE("start, stop and restart are one command each, and run no longer aliases start") {
  const fs::path cli = find_cli();
  REQUIRE(fs::is_regular_file(cli));
  const auto table = nlohmann::json::parse(core::ProcessRunner::run_sync({cli.string(), "commands", "--json"}).stdout_text);
  int starts = 0;
  for (const auto& c : table) {
    if (c["name"] == "start") ++starts;
    if (c["name"] == "run") CHECK(c["aliases"].empty());
  }
  CHECK(starts == 1);
}

TEST_CASE("start names what it could not find, and a library is not a program") {
  const fs::path cli = find_cli();
  REQUIRE(fs::is_regular_file(cli));
  const auto missing = core::ProcessRunner::run_sync({cli.string(), "start", "no-such-thing-xyz"});
  CHECK(missing.exit_code == 1);
  CHECK(missing.stderr_text.find("neither a backend nor a program") != std::string::npos);
  const auto lib = core::ProcessRunner::run_sync({cli.string(), "start", "paperboat-shared"});
  CHECK(lib.exit_code == 1);
  CHECK(lib.stderr_text.find("is a library") != std::string::npos);
  const auto run_lib = core::ProcessRunner::run_sync({cli.string(), "run", "paperboat-shared"});  // run knows programs too
  CHECK(run_lib.stderr_text.find("is a library") != std::string::npos);
}

TEST_CASE("list --json says what kind each running thing is") {
  const fs::path cli = find_cli();
  REQUIRE(fs::is_regular_file(cli));
  const auto rows = nlohmann::json::parse(core::ProcessRunner::run_sync({cli.string(), "list", "--json"}).stdout_text);
  REQUIRE(rows.is_array());
  for (const auto& r : rows) {
    CHECK((r["kind"] == "backend" || r["kind"] == "app"));
    CHECK(r.contains("port"));
    CHECK(r.contains("pid"));
  }
}
