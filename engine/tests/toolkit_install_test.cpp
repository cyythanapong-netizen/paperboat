// Installer coverage for toolkit installation, across every entry in toolkits.toml.
//
// Installing a toolkit is the one path that clones or copies into the tree, writes a marker,
// keeps a backup, and then rebuilds the host; it runs once per toolkit and, when it fails,
// only for a user. Fixtures here are local folders, so the whole lifecycle runs with no git
// remote, no network, and no dependency on the real checkout.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/toolkit/installer.hpp"

namespace fs = std::filesystem;
using paperboat::toolkit::HostBuild;
using paperboat::toolkit::ToolkitEntry;
using paperboat::toolkit::install_toolkit;
using paperboat::toolkit::list_toolkits;
using paperboat::toolkit::load_toolkits_manifest;
using paperboat::toolkit::uninstall_toolkit;

namespace {

// tests/ is <repo>/engine/tests, and PAPERBOAT_TEST_DATA is <repo>/engine/tests/data.
const fs::path kRepoRoot = fs::path(PAPERBOAT_TEST_DATA).parent_path().parent_path().parent_path();

void write_file(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
  REQUIRE(out.good());
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The shape the real optional packages have: a catalog entry that names a source, and a folder
// with a register list, one folder per block, and its examples.
struct Fixture {
  fs::path root;     // the repository the installer writes into
  fs::path backend;  // the backend asking for the toolkit
  fs::path source;   // the local toolkit folder the catalog points at
  fs::path target;   // root/engine/toolkit/<name>, where the install lands
};

Fixture make_fixture(const fs::path& tmp, const ToolkitEntry& entry) {
  Fixture f;
  f.root = tmp / ("repo-" + entry.name);
  f.backend = tmp / ("backend-" + entry.name);
  f.source = tmp / ("source-" + entry.name);
  f.target = f.root / "engine" / "toolkit" / entry.name;

  std::ostringstream manifest;
  manifest << "[[toolkits]]\nname = \"" << entry.name << "\"\nrepo = \"" << f.source.generic_string()
           << "\"\ndescription = \"" << entry.description << "\"\noptional = true\nengine = \"" << entry.engine
           << "\"\n";
  if (!entry.prefix.empty()) manifest << "prefix = \"" << entry.prefix << "\"\n";
  manifest << "blocks = [";
  for (std::size_t i = 0; i < entry.blocks.size(); ++i) {
    manifest << (i ? ", " : "") << "\"" << entry.blocks[i] << "\"";
  }
  manifest << "]\n";
  write_file(f.root / "toolkits.toml", manifest.str());

  // The backend requires the toolkit: list_toolkits reports `required` from this file.
  write_file(f.backend / "toolkit_requirements.toml", "toolkits = [\"" + entry.name + "\"]\n");

  write_file(f.source / "toolkit.toml",
             "name = \"" + entry.name + "\"\noptional = true\nregister = [\"register_" + entry.name + "\"]\n");
  // A real package is a CMake project the host adds as a subdirectory after install.
  write_file(f.source / "CMakeLists.txt",
             "add_library(paperboat_fixture_" + entry.name + " INTERFACE)\n");
  for (const auto& block : entry.blocks) {
    write_file(f.source / "src" / entry.name / block / (block + ".cpp"), "// fixture block\n");
  }
  write_file(f.source / "examples" / (entry.prefix.empty() ? "demo" : entry.prefix + "-01-demo") / "descriptor.toml",
             "domain = \"" + entry.name + "\"\n");
  return f;
}

// A host project like the engine: it fails unless the toolkit is installed and can be added.
// `builds` false makes configure succeed but the build step fail, as a broken host would.
std::string host_cmake(const Fixture& fixture, bool builds) {
  std::ostringstream out;
  out << "cmake_minimum_required(VERSION 3.20)\nproject(paperboat_host_check LANGUAGES NONE)\n"
      << "if(NOT EXISTS \"" << (fixture.target / "toolkit.toml").generic_string() << "\")\n"
      << "  message(FATAL_ERROR \"the toolkit is not installed\")\n"
      << "endif()\n";
  if (builds) {
    out << "add_subdirectory(\"" << fixture.target.generic_string() << "\" \"${CMAKE_BINARY_DIR}/toolkit\")\n"
        << "add_custom_target(host_check ALL COMMAND ${CMAKE_COMMAND} -E echo \"host built\")\n";
  } else {
    out << "add_custom_target(host_check ALL\n"
        << "  COMMAND ${CMAKE_COMMAND} -E cat \"${CMAKE_CURRENT_SOURCE_DIR}/missing.txt\")\n";
  }
  return out.str();
}

HostBuild no_rebuild() {
  HostBuild host;
  host.skip_rebuild = true;
  return host;
}

}  // namespace

TEST_CASE("every toolkit in toolkits.toml installs, lists and uninstalls") {
  const auto catalog = load_toolkits_manifest(kRepoRoot / "toolkits.toml");
  REQUIRE_FALSE(catalog.empty());

  const fs::path tmp = fs::temp_directory_path() / "paperboat_toolkit_matrix";
  std::error_code ec;
  fs::remove_all(tmp, ec);

  for (const auto& entry : catalog) {
    DYNAMIC_SECTION("toolkit " << entry.name) {
      REQUIRE_FALSE(entry.blocks.empty());
      const Fixture fixture = make_fixture(tmp, entry);

      std::vector<std::string> log;
      const auto logger = [&](std::string_view level, std::string_view text) {
        log.emplace_back(std::string(level) + ": " + std::string(text));
      };

      // Every toolkit's install must leave the host buildable with that package added.
      const fs::path host_src = tmp / ("host-" + entry.name);
      const fs::path host_build = tmp / ("host-build-" + entry.name);
      write_file(host_src / "CMakeLists.txt", host_cmake(fixture, true));
      HostBuild host;
      host.source_dir = host_src;
      host.build_dir = host_build;

      const auto installed = install_toolkit(entry.name, "local", fixture.root, host, logger);
      CAPTURE(installed.message);
      CHECK(installed.ok);
      CHECK(fs::is_regular_file(host_build / "CMakeCache.txt"));
      CHECK(fs::is_regular_file(fixture.target / "toolkit.toml"));
      for (const auto& block : entry.blocks) {
        CHECK(fs::is_regular_file(fixture.target / "src" / entry.name / block / (block + ".cpp")));
      }
      CHECK(fs::is_directory(fixture.target / "examples"));
      CHECK_FALSE(log.empty());

      // The marker records the source; list_toolkits reports it installed and required.
      const fs::path marker = fixture.target / ".paperboat-toolkit.json";
      REQUIRE(fs::is_regular_file(marker));
      const auto record = nlohmann::json::parse(read_file(marker));
      CHECK(record["version"] == "local");
      CHECK(record["ref"] == "local");
      CHECK(record["commit"] == "local");

      const auto list = list_toolkits(fixture.root, fixture.backend);
      REQUIRE(list.size() == 2);
      CHECK(list[0].builtin);
      CHECK(list[0].installed);  // the built-in entry is always installed
      CHECK(list[1].name == entry.name);
      CHECK(list[1].installed);
      CHECK(list[1].required);
      CHECK(list[1].version == "local");
      CHECK(list[1].blocks == entry.blocks);

      // Installing over an install (the version-switch path) rebuilds again and replaces the tree.
      const auto again = install_toolkit(entry.name, "local", fixture.root, host, logger);
      CAPTURE(again.message);
      CHECK(again.ok);
      CHECK(fs::is_regular_file(fixture.target / "toolkit.toml"));
      CHECK(fs::is_regular_file(marker));

      const auto removed = uninstall_toolkit(entry.name, fixture.root, no_rebuild(), logger);
      CAPTURE(removed.message);
      CHECK(removed.ok);
      CHECK_FALSE(fs::exists(fixture.target));
      CHECK_FALSE(list_toolkits(fixture.root, fixture.backend)[1].installed);
    }
  }
  fs::remove_all(tmp, ec);
}

TEST_CASE("installing a toolkit rebuilds the host, and a broken host build fails the install") {
  const auto catalog = load_toolkits_manifest(kRepoRoot / "toolkits.toml");
  REQUIRE_FALSE(catalog.empty());
  const ToolkitEntry& entry = catalog.front();

  const fs::path tmp = fs::temp_directory_path() / "paperboat_toolkit_rebuild";
  std::error_code ec;
  fs::remove_all(tmp, ec);

  const Fixture fixture = make_fixture(tmp, entry);
  const fs::path toolkit_file = fixture.target / "toolkit.toml";
  const fs::path host_src = tmp / "host-src";
  const fs::path host_build = tmp / "host-build";

  HostBuild host;
  host.source_dir = host_src;
  host.build_dir = host_build;
  std::vector<std::string> log;
  const auto logger = [&](std::string_view level, std::string_view text) {
    log.emplace_back(std::string(level) + ": " + std::string(text));
  };

  write_file(host_src / "CMakeLists.txt", host_cmake(fixture, true));
  const auto installed = install_toolkit(entry.name, "local", fixture.root, host, logger);
  CAPTURE(installed.message);
  CHECK(installed.ok);
  CHECK(fs::is_regular_file(host_build / "CMakeCache.txt"));
  CHECK(fs::is_regular_file(toolkit_file));

  // A host that no longer builds fails the install with the rebuild's own message, but leaves
  // the toolkit in place: the user can fix the build and install again.
  write_file(host_src / "CMakeLists.txt", host_cmake(fixture, false));
  const auto broken = install_toolkit(entry.name, "local", fixture.root, host, logger);
  CAPTURE(broken.message);
  CHECK_FALSE(broken.ok);
  CHECK(broken.message.find("rebuild failed") != std::string::npos);
  CHECK(broken.message.find("cmake build failed") != std::string::npos);
  CHECK(fs::is_regular_file(toolkit_file));

  // Once fixed, the next install rebuilds and succeeds.
  write_file(host_src / "CMakeLists.txt", host_cmake(fixture, true));
  const auto recovered = install_toolkit(entry.name, "local", fixture.root, host, logger);
  CAPTURE(recovered.message);
  CHECK(recovered.ok);

  fs::remove_all(tmp, ec);
}

TEST_CASE("toolkits.toml advertises the blocks each installed toolkit provides") {
  const auto catalog = load_toolkits_manifest(kRepoRoot / "toolkits.toml");
  REQUIRE_FALSE(catalog.empty());

  for (const auto& entry : catalog) {
    DYNAMIC_SECTION("toolkit " << entry.name) {
      // The catalog is what the app lists before install: a toolkit with no blocks there looks
      // empty, and after install only the registry knows better.
      CHECK_FALSE(entry.blocks.empty());
      auto sorted = entry.blocks;
      std::ranges::sort(sorted);
      CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
      for (const auto& block : entry.blocks) {
        CHECK_FALSE(block.empty());
        CHECK(std::ranges::all_of(block, [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }));
      }

      // Only check the package itself when this checkout has it installed (optional toolkits
      // are gitignored, so CI usually does not).
      const fs::path toolkit_dir = kRepoRoot / "engine" / "toolkit" / entry.name;
      if (!fs::is_regular_file(toolkit_dir / "toolkit.toml")) continue;
      const fs::path blocks_dir = toolkit_dir / "src" / entry.name;
      if (!fs::is_directory(blocks_dir)) continue;

      // The engine's block-folder convention: src/<package>/<block>/<block>.cpp.
      std::vector<std::string> folders;
      for (const auto& item : fs::directory_iterator(blocks_dir)) {
        if (!item.is_directory()) continue;
        const std::string name = item.path().filename().string();
        if (fs::is_regular_file(item.path() / (name + ".cpp"))) folders.push_back(name);
      }
      for (const auto& folder : folders) {
        INFO("toolkit " << entry.name << " ships block \"" << folder << "\"");
        CHECK(std::ranges::find(entry.blocks, folder) != entry.blocks.end());
      }

      // And the reverse, for packages that do not define blocks in TOML.
      if (read_file(toolkit_dir / "toolkit.toml").find("definitions") == std::string::npos) {
        for (const auto& block : entry.blocks) {
          INFO("toolkit " << entry.name << " lists block \"" << block << "\"");
          CHECK(fs::is_directory(blocks_dir / block));
        }
      }
    }
  }
}
