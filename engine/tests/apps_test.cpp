#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "paperboat/toolkit/apps.hpp"

namespace fs = std::filesystem;
namespace tk = paperboat::toolkit;

TEST_CASE("apps.toml parses, and an app's name and repo are required") {
  const auto apps = tk::parse_apps_manifest(R"(
[[apps]]
name = "lib"
repo = "https://example.com/lib.git"
build = ["npm ci"]
[[apps]]
name = "ui"
repo = "https://example.com/ui.git"
deps = ["lib"]
build = ["npm ci", "npm run build"]
output = "dist"
)");
  REQUIRE(apps.size() == 2);
  CHECK(apps[1].deps == std::vector<std::string>{"lib"});
  CHECK(apps[1].build.size() == 2);
  CHECK(apps[1].output == "dist");
  CHECK_THROWS(tk::parse_apps_manifest("[[apps]]\nname = \"x\"\n"));
}

TEST_CASE("pkg lifecycle: deps first, versions from tags, update, uninstall guard") {
  const fs::path tmp = fs::temp_directory_path() / "paperboat_apps_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  const auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };
  const auto repo = [&](const std::string& name) {  // a git repo with one file, tagged v0.1.0
    const fs::path d = tmp / "remote" / name;
    fs::create_directories(d);
    std::ofstream(d / "a.txt") << name;
    const std::string g = "git -C '" + d.string() + "' ";
    REQUIRE(sh(g + "init -q && " + g + "config user.email t@e.com && " + g + "config user.name t && " + g +
               "add -A && " + g + "commit -qm one && " + g + "tag v0.1.0"));
    return d;
  };
  const fs::path lib = repo("lib"), ui = repo("ui");
  fs::create_directories(tmp / "root" / "engine" / "toolkit");
  std::ofstream(tmp / "root" / "apps.toml")
      << "[[apps]]\nname = \"lib\"\nrepo = \"" << lib.string() << "\"\nbuild = [\"true\"]\n"
      << "[[apps]]\nname = \"ui\"\nrepo = \"" << ui.string() << "\"\ndeps = [\"lib\"]\n"
      << "build = [\"mkdir dist\"]\noutput = \"dist\"\n";
  setenv("PAPERBOAT_HOME", (tmp / "home").c_str(), 1);
  const fs::path root = tmp / "root";

  CHECK(tk::list_app_versions("ui", root) == std::vector<std::string>{"0.1.0"});
  CHECK(tk::app_output("ui").empty());

  CHECK(tk::install_app("ui", {}, root).ok);  // installs lib first, then ui at v0.1.0
  CHECK(fs::is_regular_file(tmp / "home" / "apps" / "lib" / "a.txt"));
  CHECK(tk::app_output("ui") == tmp / "home" / "apps" / "ui" / "dist");
  const auto list = tk::list_apps(root);
  REQUIRE(list.size() == 2);
  CHECK(list[1].installed);
  CHECK(list[1].version == "0.1.0");

  CHECK_FALSE(tk::uninstall_app("lib", root).ok);  // ui needs it
  CHECK(tk::uninstall_app("ui", root).ok);
  CHECK(tk::uninstall_app("lib", root).ok);
  CHECK_FALSE(tk::install_app("ui", "9.9.9", root).ok);  // no such tag: nothing left half-installed
  CHECK_FALSE(fs::exists(tmp / "home" / "apps" / "ui"));
  CHECK_FALSE(tk::install_app("nope", {}, root).ok);

  unsetenv("PAPERBOAT_HOME");
  fs::remove_all(tmp, ec);
}
