// `paperboat new`: the scaffolder writes a backend that is complete and consistent (its
// manifest, descriptor and expected.json agree), and never overwrites anything. The full
// build-check-run of both templates is done by hand / in CI (`paperboat new x && paperboat check`).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/platform.hpp"
#include "paperboat/toolkit/registry.hpp"
#include "paperboat_new.hpp"

using Catch::Matchers::ContainsSubstring;
namespace core = paperboat::core;
namespace cli = paperboat::cli;
namespace tk = paperboat::toolkit;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// A fresh, absent folder under the temp dir; removed again when the test ends.
struct TempDir {
  fs::path path;
  explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / ("pb_new_test_" + name)) {
    fs::remove_all(path);
  }
  ~TempDir() { fs::remove_all(path); }
};

std::set<std::string> written(const cli::NewResult& r) {
  std::set<std::string> out;
  for (const auto& f : r.files) out.insert(f.generic_string());
  return out;
}

// What the descriptor's starter graph needs beyond the toolkit: the domain's Scale block.
struct StubScale final : tk::Block {
  explicit StubScale(const tk::Values&) {}
  tk::Values step(const tk::Context&, const tk::Values&) override { return {}; }
};

// The CLI built with these tests: <build>/tests/paperboat_tests -> <build>/tools/paperboat.
fs::path cli_binary() {
  return paperboat::platform::exe_path().parent_path().parent_path() / "tools" / "paperboat";
}

}  // namespace

TEST_CASE("paperboat new: both templates are embedded, with a manifest, descriptor and expected.json") {
  std::set<std::string> cpp, python;
  for (const auto& t : cli::templates()) {
    CHECK_FALSE(t.content.empty());
    (t.language == "cpp" ? cpp : python).insert(std::string(t.path));
  }
  for (const auto* files : {&cpp, &python}) {
    CHECK(files->contains("backend.toml"));
    CHECK(files->contains("descriptor.toml"));
    CHECK(files->contains("expected.json"));
  }
  CHECK(cpp.contains("CMakeLists.txt"));
  CHECK(cpp.contains("main.cpp"));
  CHECK(python.contains("server.py"));
}

TEST_CASE("paperboat new writes a C++ backend whose manifest, descriptor and golden agree") {
  TempDir tmp("cpp");
  const auto res = cli::new_backend({.name = "my-thing", .language = "cpp", .dir = tmp.path});
  REQUIRE(res.ok);
  CHECK(res.dir == tmp.path);
  CHECK(written(res) == std::set<std::string>{".gitignore", "CMakeLists.txt", "backend.toml", "descriptor.toml",
                                              "domain/blocks.hpp", "expected.json", "main.cpp"});

  // Every placeholder is substituted.
  const std::regex placeholder("@[a-z]+@");
  for (const auto& f : res.files) {
    INFO(f.string());
    CHECK_FALSE(std::regex_search(slurp(tmp.path / f), placeholder));
  }

  std::string err;
  const auto m = core::BackendManager::load_manifest(tmp.path, &err);
  INFO(err);
  REQUIRE(m.has_manifest);
  CHECK(m.name == "my-thing");
  CHECK(m.language == "cpp");
  CHECK(m.run == "build/paperboat-my-thing");
  CHECK(m.port == cli::kNewBackendPort);
  CHECK(m.golden == "expected.json");
  CHECK(core::BackendManager::detect_backend_kind(tmp.path) == core::BackendKind::Cpp);

  const auto expected = nlohmann::json::parse(slurp(tmp.path / "expected.json"));
  CHECK(expected["run"] == m.run);  // golden.py runs the program the build makes
  CHECK(expected["status"] == "done");
  CHECK(expected["signals"].contains("result"));

  const std::string cmake = slurp(tmp.path / "CMakeLists.txt");
  CHECK_THAT(cmake, ContainsSubstring("add_executable(paperboat-my-thing main.cpp)"));
  CHECK_THAT(cmake, ContainsSubstring("project(my_thing "));
  CHECK_THAT(cmake, ContainsSubstring("find_package(Paperboat"));  // builds from an installed SDK too
  CHECK_THAT(slurp(tmp.path / "main.cpp"), ContainsSubstring(".engine = \"my-thing\""));
  CHECK_THAT(slurp(tmp.path / "domain" / "blocks.hpp"), ContainsSubstring("define_block<Scale>(\"my-thing\""));
}

TEST_CASE("paperboat new's starter graph loads against the toolkit and the domain's block") {
  TempDir tmp("graph");
  const auto res = cli::new_backend({.name = "graph-probe", .language = "cpp", .dir = tmp.path});
  REQUIRE(res.ok);
  tk::Registry registry = tk::builtins();
  registry.add(tk::define_block<StubScale>("graph-probe", {.type = "scale", .inputs = {{"u"}}, .outputs = {{"y"}},
                                                           .params = {{.id = "gain", .default_value = 2}}}));
  const auto catalog = tk::load_descriptor_file(tmp.path / "descriptor.toml", registry);
  CHECK(catalog.domain == "graph-probe");
  CHECK_FALSE(catalog.graph.empty());
  const auto graph = nlohmann::json::parse(catalog.graph);
  bool has_result = false;
  for (const auto& n : graph["nodes"]) has_result = has_result || n["id"] == "result";
  CHECK(has_result);  // the node expected.json checks
}

TEST_CASE("paperboat new writes a Python backend that check's manifest and catalog stages pass") {
  TempDir tmp("python");
  const auto res = cli::new_backend({.name = "py-thing", .language = "python", .dir = tmp.path});
  REQUIRE(res.ok);
  CHECK(written(res) == std::set<std::string>{".gitignore", "backend.toml", "descriptor.toml", "domain/__init__.py",
                                              "domain/blocks.py", "expected.json", "requirements.txt", "server.py"});
  const auto m = core::BackendManager::load_manifest(tmp.path);
  REQUIRE(m.has_manifest);
  CHECK(m.language == "python");
  CHECK(m.run == "server.py");
  CHECK(core::BackendManager::detect_backend_kind(tmp.path) == core::BackendKind::Python);
  CHECK(nlohmann::json::parse(slurp(tmp.path / "expected.json"))["run"] == "server.py");
  CHECK_THAT(slurp(tmp.path / "server.py"), ContainsSubstring("engine=\"py-thing\""));

  const auto check = core::BackendManager::check_backend(tmp.path, /*fast=*/true);
  INFO((check.errors.empty() ? std::string{} : check.errors.front()));
  CHECK(check.manifest_ok);
  CHECK(check.catalog_ok);
  CHECK(check.ok);
}

TEST_CASE("paperboat new writes a Julia backend whose manifest and project agree") {
  TempDir tmp("julia");
  const auto res = cli::new_backend({.name = "jl-thing", .language = "julia", .dir = tmp.path});
  REQUIRE(res.ok);
  CHECK(written(res) == std::set<std::string>{".gitignore", "Project.toml", "backend.toml", "descriptor.toml",
                                              "domain/blocks.jl", "expected.json", "server.jl"});
  const auto m = core::BackendManager::load_manifest(tmp.path);
  REQUIRE(m.has_manifest);
  CHECK(m.language == "julia");
  CHECK(m.run == "server.jl");
  CHECK(core::BackendManager::detect_backend_kind(tmp.path) == core::BackendKind::Julia);
  CHECK_THAT(slurp(tmp.path / "server.jl"), ContainsSubstring("Paperboat.main"));
  // Project.toml points at the checkout's Paperboat.jl, which `deps install` resolves.
  CHECK_THAT(slurp(tmp.path / "Project.toml"), ContainsSubstring("[sources]"));
  CHECK_THAT(slurp(tmp.path / "Project.toml"), ContainsSubstring("julia/Paperboat"));
  CHECK_THAT(slurp(tmp.path / "domain/blocks.jl"), ContainsSubstring("ctx::Paperboat.Context"));
  const auto expected = nlohmann::json::parse(slurp(tmp.path / "expected.json"));
  CHECK(expected["run"] == "server.jl");
  CHECK(expected["signals"]["result"] == 6.0);
}

TEST_CASE("paperboat new never overwrites: a non-empty folder is refused, an empty one is fine") {
  TempDir tmp("existing");
  fs::create_directories(tmp.path);
  CHECK(cli::new_backend({.name = "empty-ok", .language = "python", .dir = tmp.path}).ok);  // existed, empty

  // Now it holds the backend: a second run refuses and leaves every file as it was.
  std::ofstream(tmp.path / "backend.toml") << "# mine\n";
  const auto again = cli::new_backend({.name = "empty-ok", .language = "cpp", .dir = tmp.path});
  CHECK_FALSE(again.ok);
  CHECK_THAT(again.error, ContainsSubstring("not empty"));
  CHECK(slurp(tmp.path / "backend.toml") == "# mine\n");
  CHECK_FALSE(fs::exists(tmp.path / "CMakeLists.txt"));

  TempDir file("a_file");
  std::ofstream(file.path) << "x";
  CHECK_THAT(cli::new_backend({.name = "x", .dir = file.path}).error, ContainsSubstring("not a folder"));
}

TEST_CASE("paperboat new checks the name and the language") {
  CHECK(cli::check_backend_name("my-backend_2").empty());
  for (const char* bad : {"", "My", "2fast", "-x", "a b", "a/b", "a.b", "über"}) {
    INFO(bad);
    CHECK_FALSE(cli::check_backend_name(bad).empty());
  }
  CHECK_FALSE(cli::check_backend_name(std::string(65, 'a')).empty());
  TempDir tmp("lang");
  const auto res = cli::new_backend({.name = "x", .language = "rust", .dir = tmp.path});
  CHECK_FALSE(res.ok);
  CHECK_THAT(res.error, ContainsSubstring("unknown language"));
  CHECK_FALSE(fs::exists(tmp.path));
}

TEST_CASE("paperboat new on the command line: flags, next steps, a path as the name") {
  const fs::path bin = cli_binary();
  REQUIRE(fs::is_regular_file(bin));
  using core::ProcessRunner;
  CHECK(ProcessRunner::run_sync({bin.string(), "new"}).exit_code == 2);
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "x", "--cpp", "--python"}).exit_code == 2);
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "x", "--rust"}).exit_code == 2);
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "x", "--dir"}).exit_code == 2);

  TempDir parent("cli");
  fs::create_directories(parent.path);
  const auto made = ProcessRunner::run_sync({bin.string(), "new", "demo", "--python"}, parent.path);
  CHECK(made.exit_code == 0);
  CHECK_THAT(made.stdout_text, ContainsSubstring("Next steps"));
  CHECK_THAT(made.stdout_text, ContainsSubstring("paperboat check"));
  CHECK_THAT(made.stdout_text, ContainsSubstring("paperboat run"));
  CHECK(core::BackendManager::load_manifest(parent.path / "demo").name == "demo");
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "demo"}, parent.path).exit_code == 1);  // not empty now

  // `paperboat build` on a Python backend: nothing to compile, not an error.
  const auto build = ProcessRunner::run_sync({bin.string(), "build", (parent.path / "demo").string()});
  CHECK(build.exit_code == 0);
  CHECK_THAT(build.stdout_text, ContainsSubstring("nothing to compile"));

  // A path: the folder is the path, the name its last part. --dir names the folder directly.
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "nested/deep-one"}, parent.path).exit_code == 0);
  CHECK(core::BackendManager::load_manifest(parent.path / "nested" / "deep-one").name == "deep-one");
  CHECK(ProcessRunner::run_sync({bin.string(), "new", "named", "--dir", "elsewhere"}, parent.path).exit_code == 0);
  CHECK(core::BackendManager::load_manifest(parent.path / "elsewhere").name == "named");
  CHECK(core::BackendManager::load_manifest(parent.path / "elsewhere").language == "cpp");  // the default
}
