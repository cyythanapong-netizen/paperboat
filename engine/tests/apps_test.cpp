#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/update.hpp"

namespace fs = std::filesystem;
namespace core = paperboat::core;
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
deps = ["lib", "kit@0.1"]
build = ["npm ci", "npm run build"]
output = "dist"
)");
  REQUIRE(apps.size() == 2);
  CHECK(apps[1].deps == std::vector<tk::AppDep>{{"lib", ""}, {"kit", "0.1"}});
  CHECK(apps[1].deps[1].spec() == "kit@0.1");
  CHECK(apps[1].build.size() == 2);
  CHECK(apps[1].output == "dist");
  CHECK_THROWS(tk::parse_apps_manifest("[[apps]]\nname = \"x\"\n"));
  for (const char* bad : {"lib@", "lib@x", "lib@0.1.2.3", "lib@^0.1", "@0.1"})  // a range is MAJOR[.MINOR[.PATCH]]
    CHECK_THROWS(tk::parse_apps_manifest(std::string("[[apps]]\nname = \"x\"\nrepo = \"r\"\ndeps = [\"") + bad + "\"]\n"));
}

TEST_CASE("an app is a program or a library, and a program says how it launches and where it listens") {
  const auto apps = tk::parse_apps_manifest(R"(
[[apps]]
name = "tool"
repo = "https://example.com/tool.git"
kind = "program"
launch = "tool-cmd"
port = 8123
[[apps]]
name = "kit"
repo = "https://example.com/kit.git"
kind = "library"
[[apps]]
name = "plain"
repo = "https://example.com/plain.git"
)");
  REQUIRE(apps.size() == 3);
  CHECK(apps[0].kind == "program");
  CHECK(apps[0].launch == "tool-cmd");
  CHECK(apps[0].port == 8123);
  CHECK(apps[1].kind == "library");
  CHECK(apps[2].kind == "program");  // the default
  CHECK_THROWS(tk::parse_apps_manifest("[[apps]]\nname = \"x\"\nrepo = \"r\"\nkind = \"service\"\n"));
}

TEST_CASE("pkg checks an app's and its deps' prerequisites before it fetches anything") {
  const fs::path tmp = fs::temp_directory_path() / "paperboat_apps_requires_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "root" / "engine" / "toolkit");
  const std::string manifest = R"(
[tools]
pb-no-such-tool = "get it from example.com"
"pb-no-a|pb-no-b" = "either one, from example.com"
[[apps]]
name = "kernel.jl"
repo = "/nonexistent/kernel.git"
kind = "library"
requires = ["git", "pb-no-a|pb-no-b"]
[[apps]]
name = "ui"
repo = "/nonexistent/ui.git"
deps = ["kernel.jl"]
requires = ["pb-no-such-tool", "pb-no-such-tool-either|git", "pb-no-such-tool"]
)";
  const auto apps = tk::parse_apps_manifest(manifest);
  REQUIRE(apps.size() == 2);
  CHECK(apps[0].prerequisites == std::vector<std::string>{"git", "pb-no-a|pb-no-b"});
  CHECK(tk::parse_tool_hints(manifest).at("pb-no-such-tool") == "get it from example.com");
  for (const char* bad : {"\"\"", "\"a||b\"", "\"|a\"", "\"a|\""})
    CHECK_THROWS(tk::parse_apps_manifest(std::string("[[apps]]\nname = \"x\"\nrepo = \"r\"\nrequires = [") + bad + "]\n"));

  std::ofstream(tmp / "root" / "apps.toml") << manifest;
  setenv("PAPERBOAT_HOME", (tmp / "home").c_str(), 1);
  const fs::path root = tmp / "root";
  const std::string missing = tk::missing_prerequisites("ui", root);
  CHECK(missing.find("pb-no-such-tool (\"ui\"): get it from example.com") != std::string::npos);
  CHECK(missing.find("one of pb-no-a or pb-no-b (\"kernel.jl\"): either one, from example.com") != std::string::npos);
  CHECK(missing.find("pb-no-such-tool-either") == std::string::npos);  // git is there
  CHECK(missing.find("pb-no-such-tool") == missing.rfind("pb-no-such-tool"));  // named once
  CHECK(tk::missing_prerequisites("nope", root).empty());

  const auto r = tk::install_app("ui", {}, root);
  CHECK_FALSE(r.ok);
  CHECK(r.message.find("needs what this machine lacks") != std::string::npos);
  CHECK_FALSE(fs::exists(tmp / "home" / "apps"));  // nothing fetched

  unsetenv("PAPERBOAT_HOME");
  fs::remove_all(tmp, ec);
}

TEST_CASE("pkg never waits at a git prompt, and a failed clone says how to give access") {
  const fs::path tmp = fs::temp_directory_path() / "paperboat_apps_clone_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "root" / "engine" / "toolkit");
  std::ofstream(tmp / "root" / "apps.toml") << "[[apps]]\nname = \"gone.jl\"\nrepo = \"" << (tmp / "missing.git").string()
                                            << "\"\nkind = \"library\"\n";
  setenv("PAPERBOAT_HOME", (tmp / "home").c_str(), 1);
  const auto r = tk::install_app("gone.jl", {}, tmp / "root");
  CHECK_FALSE(r.ok);
  CHECK(r.message.find("cannot clone") != std::string::npos);
  CHECK(r.message.find("PAPERBOAT_APP_REPO_GONE_JL") != std::string::npos);
  REQUIRE(std::getenv("GIT_TERMINAL_PROMPT") != nullptr);
  CHECK(std::string(std::getenv("GIT_TERMINAL_PROMPT")) == "0");
  unsetenv("PAPERBOAT_HOME");
  fs::remove_all(tmp, ec);
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

  // The channel sticks: a main install stays on main through a plain update; "latest" moves it to releases.
  CHECK(tk::install_app("lib", "main", root).ok);
  CHECK(tk::install_app("lib", {}, root).ok);
  CHECK(tk::list_apps(root)[0].version == "dev");
  // A main install tracks the remote's default branch, whatever it is called: a new commit there is an update.
  CHECK_FALSE(tk::app_outdated("lib", root));
  std::ofstream(lib / "b.txt") << "b";
  CHECK(sh("git -C '" + lib.string() + "' add -A && git -C '" + lib.string() + "' commit -qm two"));
  CHECK(tk::app_outdated("lib", root));
  CHECK(tk::install_app("lib", "latest", root).ok);
  CHECK(tk::list_apps(root)[0].version == "0.1.0");

  unsetenv("PAPERBOAT_HOME");
  fs::remove_all(tmp, ec);
}

TEST_CASE("update: a checkout is fast-forwarded, and refused when dirty or diverged") {
  const fs::path tmp = fs::temp_directory_path() / "paperboat_update_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "origin");
  const auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };
  const auto git = [](const fs::path& d) { return "git -C '" + d.string() + "' "; };
  const auto commit = [&](const fs::path& d, const std::string& file, const std::string& msg) {
    std::ofstream(d / file) << msg;
    return sh(git(d) + "add -A && " + git(d) + "-c user.email=t@e.com -c user.name=t commit -qm " + msg);
  };
  REQUIRE(sh(git(tmp / "origin") + "init -q -b main"));
  REQUIRE(commit(tmp / "origin", "a.txt", "one"));
  REQUIRE(sh("git clone -q '" + (tmp / "origin").string() + "' '" + (tmp / "work").string() + "'"));
  const fs::path work = tmp / "work";

  CHECK(tk::check_checkout(tmp).error == "not a git checkout");
  auto st = tk::check_checkout(work);
  CHECK(st.error.empty());
  CHECK(st.branch == "main");
  CHECK(st.behind == 0);
  CHECK(tk::pull_checkout(work).message == "up to date");

  REQUIRE(commit(tmp / "origin", "b.txt", "two"));
  st = tk::check_checkout(work);
  CHECK(st.behind == 1);
  REQUIRE(st.incoming.size() == 1);
  CHECK(st.incoming[0] == "two");

  std::ofstream(work / "a.txt") << "edited";  // a tracked file with changes: no pull
  CHECK(tk::check_checkout(work).dirty);
  CHECK_FALSE(tk::pull_checkout(work).ok);
  REQUIRE(sh(git(work) + "checkout -q a.txt"));

  std::vector<std::string> changed;
  CHECK(tk::pull_checkout(work, &changed).ok);
  CHECK(changed == std::vector<std::string>{"b.txt"});
  CHECK(fs::is_regular_file(work / "b.txt"));
  CHECK(tk::check_checkout(work).behind == 0);

  REQUIRE(commit(tmp / "origin", "c.txt", "three"));  // both sides moved: diverged
  REQUIRE(commit(work, "d.txt", "local"));
  const auto r = tk::pull_checkout(work);
  CHECK_FALSE(r.ok);
  CHECK(r.message.starts_with("diverged"));

  fs::remove_all(tmp, ec);
}

TEST_CASE("pkg pairing: a dep resolves in its range, and an installed one outside it fails or is reinstalled") {
  const fs::path tmp = fs::temp_directory_path() / "paperboat_apps_pairing_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  const auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };
  const auto git = [](const fs::path& d) { return "git -C '" + d.string() + "' -c user.email=t@e.com -c user.name=t "; };
  const auto tag = [&](const fs::path& d, const std::string& version) {  // a commit with version.txt, tagged v<version>
    std::ofstream(d / "version.txt") << version;
    return sh(git(d) + "add -A && " + git(d) + "commit -qm " + version + " && " + git(d) + "tag v" + version);
  };
  const fs::path lib = tmp / "remote" / "lib", ui = tmp / "remote" / "ui";
  for (const auto& d : {lib, ui}) {
    fs::create_directories(d);
    REQUIRE(sh(git(d) + "init -q -b main"));
  }
  for (const char* v : {"0.1.0-alpha.1", "0.1.0-alpha.2", "0.2.0"}) REQUIRE(tag(lib, v));  // 0.1 has prereleases only
  for (const char* v : {"0.1.0-alpha.1", "0.1.0"}) REQUIRE(tag(ui, v));
  fs::create_directories(tmp / "root" / "engine" / "toolkit");
  std::ofstream(tmp / "root" / "apps.toml")
      << "[[apps]]\nname = \"lib\"\nrepo = \"" << lib.string() << "\"\nbuild = [\"true\"]\n"
      << "[[apps]]\nname = \"ui\"\nrepo = \"" << ui.string() << "\"\ndeps = [\"lib@0.1\"]\nbuild = [\"true\"]\n";
  setenv("PAPERBOAT_HOME", (tmp / "home").c_str(), 1);
  const fs::path root = tmp / "root", home = tmp / "home" / "apps";
  const auto version_of = [&](const std::string& app) {
    for (const auto& a : tk::list_apps(root))
      if (a.name == app) return a.installed ? a.version : std::string("-");
    return std::string("?");
  };

  CHECK(tk::list_app_versions("lib", root) == std::vector<std::string>{"0.2.0"});  // releases only by default
  CHECK(tk::list_app_versions("lib", root, true) ==
        std::vector<std::string>{"0.2.0", "0.1.0-alpha.2", "0.1.0-alpha.1"});

  SECTION("a release app takes no prerelease of its dep") {
    const auto r = tk::install_app("ui", "0.1.0", root);
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("no release in that range") != std::string::npos);
    CHECK(version_of("lib") == "-");
    CHECK(version_of("ui") == "-");
  }

  SECTION("a prerelease app pulls the newest dep prerelease in range, and records it") {
    REQUIRE(tk::install_app("ui", "0.1.0-alpha.1", root).ok);
    CHECK(version_of("lib") == "0.1.0-alpha.2");  // not 0.2.0, and no manual pre-install
    CHECK(version_of("ui") == "0.1.0-alpha.1");
    const auto list = tk::list_apps(root);
    CHECK(list[1].built_with == std::map<std::string, std::string>{{"lib", "0.1.0-alpha.2"}});

    // the dep stays in the range of the installed app that needs it
    const auto r = tk::install_app("lib", "0.2.0", root);
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("outside 0.1") != std::string::npos);
    CHECK(version_of("lib") == "0.1.0-alpha.2");
    CHECK_FALSE(tk::app_outdated("lib", root));  // 0.2.0 is newer, but not for ui
    REQUIRE(tag(lib, "0.1.0-alpha.3"));
    CHECK(tk::app_outdated("lib", root));
    REQUIRE(tk::install_app("lib", {}, root).ok);  // what `pkg update lib` does
    CHECK(version_of("lib") == "0.1.0-alpha.3");

    CHECK(tk::app_outdated("ui", root));  // 0.1.0 is a newer release of ui itself
    CHECK(tk::install_app("lib", "0.2.0", root, {}, true).ok);  // forced out of range
    CHECK(version_of("lib") == "0.2.0");
  }

  SECTION("an installed dep at another MAJOR.MINOR fails the install, or is reinstalled with force") {
    REQUIRE(tk::install_app("lib", "0.2.0", root).ok);
    auto r = tk::install_app("ui", "0.1.0-alpha.1", root);
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("needs \"lib@0.1\", and 0.2.0 is installed") != std::string::npos);
    CHECK(r.message.find("--force") != std::string::npos);
    CHECK(version_of("lib") == "0.2.0");
    CHECK(version_of("ui") == "-");

    r = tk::install_app("ui", "0.1.0-alpha.1", root, {}, true);
    CHECK(r.ok);
    CHECK(r.message.find("with lib 0.1.0-alpha.2") != std::string::npos);
    CHECK(version_of("lib") == "0.1.0-alpha.2");
    CHECK(version_of("ui") == "0.1.0-alpha.1");
    std::ifstream marker(home / "lib" / "version.txt");
    std::string text;
    marker >> text;
    CHECK(text == "0.1.0-alpha.2");
  }

  SECTION("a dev app takes the newest dep in range, prereleases included") {
    REQUIRE(tk::install_app("ui", "dev", root).ok);
    CHECK(version_of("ui") == "dev");
    CHECK(version_of("lib") == "0.1.0-alpha.2");
  }

  SECTION("a version with no tag is refused before any dep is installed") {
    CHECK_FALSE(tk::install_app("ui", "0.1.0-alpha.9", root).ok);
    CHECK(version_of("lib") == "-");
  }

  unsetenv("PAPERBOAT_HOME");
  fs::remove_all(tmp, ec);
}

TEST_CASE("get_examples finds the examples an installed toolkit ships") {
  unsetenv("PAPERBOAT_ROOT");
  unsetenv("PAPERBOAT_EXAMPLES_DIR");
  const fs::path tmp = fs::temp_directory_path() / "paperboat_toolkit_examples_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  const fs::path root = tmp / "repo";
  const fs::path examples = root / "paperboat-examples";
  fs::create_directories(root / "engine" / "toolkit" / "foo" / "examples" / "foo-01-demo");
  fs::create_directories(examples);
  std::ofstream(root / "toolkits.toml") << "[[toolkits]]\nname = \"foo\"\nrepo = \"https://example.com/foo.git\"\n";
  std::ofstream(root / "engine" / "toolkit" / "foo" / "examples" / "foo-01-demo" / "backend.toml")
      << "[backend]\nformat = 0\nname = \"foo-01-demo\"\nversion = \"0.1.0\"\nengine = \">=0.1\"\n"
         "language = \"cpp\"\nrun = \"build/foo\"\nport = 8791\ndescriptor = \"descriptor.toml\"\n";
  std::ofstream(root / "engine" / "toolkit" / "foo" / "examples" / "foo-01-demo" / "descriptor.toml")
      << "domain = \"foo\"\n[graph]\nnodes = []\nedges = []\n";

  const auto list = core::BackendManager::get_examples(examples);
  const core::ExampleBackend* found = nullptr;
  for (const auto& e : list)
    if (e.name == "foo-01-demo") found = &e;
  REQUIRE(found != nullptr);
  CHECK(found->port == 8791);
  fs::remove_all(tmp, ec);
}
