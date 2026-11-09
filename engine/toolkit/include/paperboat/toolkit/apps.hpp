#pragma once

// Apps that live in other repositories (squero-studio-om, paperboat-shared): declared in apps.toml
// at the repository root, fetched with git at a release tag (or main) into a per-user store, and
// built there. The package manager behind `paperboat pkg` and `paperboat start <program>`.

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/installer.hpp"

namespace paperboat::toolkit {

// A dep in apps.toml: "paperboat-shared" (any version) or "paperboat-shared@0.1" (a range: any 0.1.x).
struct AppDep {
  std::string name;
  std::string range;  // empty: any version
  bool operator==(const AppDep&) const = default;
  std::string spec() const { return range.empty() ? name : name + "@" + range; }
};

struct AppEntry {
  std::string name;
  std::string repo;
  std::string description;
  std::vector<AppDep> deps;        // apps that must sit next to this one when it builds
  std::vector<std::string> build;  // commands run in the app's folder after the clone
  std::string output;              // folder the build makes (a program serves it)
  std::string kind = "program";    // "program" runs (start/stop it); "library" only builds
  std::string launch;              // what runs a program: a paperboat command or a binary path
  int port = 0;                    // where a running program listens (`stop` and `list` find it there)
  bool installed = false;
  std::string version;             // installed: a release ("0.1.0") or "dev" (main)
  std::map<std::string, std::string> built_with;  // installed: the version of each dep it was built against
};

// The store: $PAPERBOAT_HOME/apps, else $XDG_DATA_HOME/paperboat/apps, else ~/.local/share/paperboat/apps.
// Every app is a folder of its own name in it, so `file:../<dep>` dependencies resolve.
std::filesystem::path apps_dir();

// Parse apps.toml text or file. Throws DescriptorError on invalid TOML or a missing name/repo.
std::vector<AppEntry> parse_apps_manifest(std::string_view toml_text);

// All apps from apps.toml with `installed` and `version` filled in.
std::vector<AppEntry> list_apps(const std::filesystem::path& repo_root = {});

// Release versions of an app's repository, newest first (git tags). Releases only, unless `include_prereleases`.
std::vector<std::string> list_app_versions(std::string_view name, const std::filesystem::path& repo_root = {},
                                           bool include_prereleases = false);

// Does a dep installed at `dep_version` pair with an app at `app_version`? In the dep's range, or (a dev
// dep, which is main) only with a dev app.
bool dep_satisfied(std::string_view dep_version, const AppDep& dep, std::string_view app_version);

// Clone `version` (empty: the newest release, else main; "dev": main) and build it. A failed install
// leaves the old one.
//
// Deps are installed first. A dep with a range resolves to its newest tag in that range, prereleases
// included when the app's own version is a prerelease (or dev); a dev app with no tag in range takes the
// dep's main. A dep that is already installed is kept when it fits; outside the range the install fails,
// or with `force` reinstalls the dep in range. The other way round, a version of this app outside the
// range of an installed app that needs it is refused without `force`, and an empty `version` picks the
// newest release that stays inside. The marker records the dep versions (`AppEntry::built_with`).
ToolkitResult install_app(std::string_view name, std::string_view version = {},
                          const std::filesystem::path& repo_root = {}, const ToolkitLog& log = {},
                          bool force = false);

// Refused while another installed app lists it as a dep.
ToolkitResult uninstall_app(std::string_view name, const std::filesystem::path& repo_root = {},
                            const ToolkitLog& log = {});

// Run an app's `build` steps from apps.toml in `dir` (a checkout of it) and check its output folder.
ToolkitResult build_app(std::string_view name, const std::filesystem::path& dir,
                        const std::filesystem::path& repo_root = {}, const ToolkitLog& log = {});

// An installed app is outdated when a newer release exists (within the ranges of installed apps that need
// it; prereleases only when one of them is a prerelease), or (a "dev" install) main moved on.
// Offline or unknown: false.
bool app_outdated(std::string_view name, const std::filesystem::path& repo_root = {});

// An installed app's output folder (its own folder when it declares none), or empty.
std::filesystem::path app_output(std::string_view name);

}  // namespace paperboat::toolkit
