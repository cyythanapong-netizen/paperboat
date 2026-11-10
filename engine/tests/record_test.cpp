// Run records (#277): what --record keeps, and how it fails when it cannot.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/record.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
namespace fs = std::filesystem;
using core::json;
using Catch::Matchers::ContainsSubstring;

namespace {

fs::path fresh_dir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  return dir;
}

json read_json(const fs::path& path) {
  std::ifstream in(path);
  json value;
  in >> value;
  return value;
}

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void unset_env(const char* name) {
#if defined(_WIN32)
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

}  // namespace

TEST_CASE("a run record keeps the request, every signal and the Station's motion (#277)") {
  const fs::path dir = fresh_dir("paperboat_record_test");
  const json request = {
      {"graph", {{"nodes", json::array({{{"id", "c"}, {"type", "constant"}}})}, {"edges", json::array()}}},
      {"station", {{"objects", json::array({{{"id", "mass"}}})}}},
      {"variables", json::array({{{"name", "level"}, {"value", 5}}})},
      {"settings", {{"duration", 1.0}}},
  };

  core::RunRecord record({.dir = dir, .tool_version = "test-1.0", .command = "paperboat run --record out", .backend_version = "0.2.0"}, "rig", request);
  record.add({{"type", "run.started"}, {"run", "run-1"}});
  record.add({{"type", "signal"}, {"node", "show"}, {"t", 0.25}, {"v", 10.0}});
  record.add({{"type", "signal"}, {"node", "scope"}, {"port", "a"}, {"t", 0.5}, {"v", 2.5}});
  record.add({{"type", "scene"}, {"entities", json::array({{{"id", "mass"}, {"shape", "box"}, {"pos", json::array({0, 0, 0})}}})}});
  record.add({{"type", "scene.patch"}, {"entities", json::array({{{"id", "mass"}, {"pos", json::array({1, 0, 0})}}})}});
  record.add({{"type", "run.finished"}, {"run", "run-1"}, {"status", "done"}});

  REQUIRE(record.finished());
  const fs::path path = dir / "record.json";
  REQUIRE(fs::is_regular_file(path));
  REQUIRE_FALSE(fs::exists(dir / "record.json.tmp"));
  const json saved = read_json(path);
  CHECK(saved.at("paperboat") == 0);
  CHECK(saved.at("run").at("id") == "run-1");
  CHECK(saved.at("run").at("status") == "done");
  CHECK(saved.at("run").at("started").get<std::string>().ends_with("Z"));
  CHECK(saved.at("backend").at("name") == "rig");
  CHECK(saved.at("backend").at("version") == "0.2.0");
  CHECK(saved.at("tool_versions").at("paperboat") == "test-1.0");
  CHECK(saved.at("command") == "paperboat run --record out");
  CHECK(saved.at("flow").at("nodes").size() == 1);
  CHECK(saved.at("station").at("objects").size() == 1);
  CHECK(saved.at("variables").at(0).at("name") == "level");
  CHECK(saved.at("signals").size() == 2);
  CHECK(saved.at("signals").at(1).at("v") == 2.5);
  CHECK(saved.at("signals").at(1).at("port") == "a");
  CHECK(saved.at("scene").at("base").size() == 1);
  CHECK(saved.at("scene").at("frames").at(0).at("t") == 0.5);
  CHECK(saved.at("scene").at("frames").at(0).at("entities").at(0).at("pos").at(0) == 1);
}

TEST_CASE("a record that cannot be written says so by name (#277)") {
  const fs::path dir = fresh_dir("paperboat_record_blocked");
  fs::create_directories(dir.parent_path());
  { std::ofstream(dir) << "not a directory"; }
  core::RunRecord record({.dir = dir, .tool_version = {}, .command = {}, .backend_version = {}}, "rig", json::object());
  CHECK_THROWS_WITH(record.write(), ContainsSubstring("cannot create record directory"));
  std::error_code ec;
  fs::remove(dir, ec);
}

TEST_CASE("PAPERBOAT_RECORD is what asks for a record (#277)") {
  unset_env("PAPERBOAT_RECORD");
  CHECK_FALSE(core::record_meta_from_env().has_value());

  const fs::path dir = fresh_dir("paperboat_record_env");
  set_env("PAPERBOAT_RECORD", dir.string());
  set_env("PAPERBOAT_VERSION", "9.9.9");
  set_env("PAPERBOAT_COMMAND", "paperboat run --record here");
  set_env("PAPERBOAT_BACKEND_VERSION", "1.2.3");
  const auto meta = core::record_meta_from_env();
  REQUIRE(meta.has_value());
  CHECK(meta->dir == dir);
  CHECK(meta->tool_version == "9.9.9");
  CHECK(meta->command == "paperboat run --record here");
  CHECK(meta->backend_version == "1.2.3");

  unset_env("PAPERBOAT_RECORD");
  unset_env("PAPERBOAT_VERSION");
  unset_env("PAPERBOAT_COMMAND");
  unset_env("PAPERBOAT_BACKEND_VERSION");
}

TEST_CASE("a headless --run keeps its record too (#277)") {
  const fs::path dir = fresh_dir("paperboat_record_headless");
  set_env("PAPERBOAT_RECORD", dir.string());
  set_env("PAPERBOAT_VERSION", "test-headless");

  const core::Backend backend{.engine = "rig", .catalog = &rig::catalog(), .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  std::ostringstream out;
  CHECK(core::run_starter(backend, out, std::vector<std::pair<std::string, std::string>>{}) == "done");

  unset_env("PAPERBOAT_RECORD");
  unset_env("PAPERBOAT_VERSION");

  const fs::path path = dir / "record.json";
  REQUIRE(fs::is_regular_file(path));
  const json saved = read_json(path);
  CHECK(saved.at("run").at("id") == "run-1");
  CHECK(saved.at("run").at("status") == "done");
  CHECK(saved.at("tool_versions").at("paperboat") == "test-headless");
  CHECK(saved.at("signals").is_array());
}
