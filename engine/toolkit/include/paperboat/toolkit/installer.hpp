#pragma once

// Optional external toolkits: declared in toolkits.toml at the repository root and installed on
// demand into engine/toolkit/<name>. Backends may declare required toolkits in
// toolkit_requirements.toml (or import/use them in descriptor.toml).

#include <compare>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit {

// One cmake build at a time in this process: two that touch the same build folder at once fail on
// each other's files. Hold this before configuring or building (the installer does, and so does
// every backend build), so an install and a restart-triggered rebuild never overlap.
std::unique_lock<std::mutex> lock_build();

struct ToolkitEntry {
  std::string name;
  std::string repo;  // a suggested source; never required, never used at launch
  std::string description;
  bool optional = true;
  bool builtin = false;
  std::string engine;
  std::string prefix;  // example folder prefix ("ocv" → ocv-01-…), from toolkits.toml
  bool installed = false;
  bool required = false;
  std::string version;
  std::vector<std::string> versions;
  std::string default_version;
  std::vector<std::string> blocks;  // block types it provides, from toolkits.toml
  std::vector<std::string> needs;  // toolkits it requires built first, from toolkits.toml (`requires`)
};

struct ToolkitInspectBlock {
  std::string type;
  std::string label;
  std::string description;
};

struct ToolkitInspectInstrument {
  std::string type;
  std::string label;
  std::string instrument;
  std::string description;
};

struct ToolkitInspectResult {
  std::string name;
  bool builtin = false;
  bool installed = false;
  bool found = false;
  std::vector<ToolkitInspectBlock> station_blocks;
  std::vector<ToolkitInspectBlock> flow_blocks;
  std::vector<ToolkitInspectInstrument> dashboard_instruments;
};

struct HostBuild {
  std::filesystem::path source_dir;
  std::filesystem::path build_dir;
  bool skip_rebuild = false;
};

struct ToolkitResult {
  bool ok = false;
  std::string message;
};

using ToolkitLog = std::function<void(std::string_view level, std::string_view text)>;

// Run a command (argv, no shell) in `cwd`, passing each output line to `log`. Returns the exit status;
// `last_line` gets the last non-empty line, `all_output` everything.
int run_command(const std::vector<std::string>& args, const std::filesystem::path& cwd = {},
                const ToolkitLog& log = {}, std::string* last_line = nullptr, std::string* all_output = nullptr);

// A release version: MAJOR.MINOR.PATCH with an optional -prerelease ("0.1.0-alpha.2"), ordered by
// SemVer precedence: 0.1.0-alpha.2 < 0.1.0-alpha.10 < 0.1.0-beta < 0.1.0 < 0.1.1.
struct ReleaseVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string prerelease;  // empty for a release

  std::strong_ordering operator<=>(const ReleaseVersion& o) const;
  bool operator==(const ReleaseVersion& o) const = default;
  std::string str() const;
};

// "0.1.0", "v0.1.0-alpha.2"; nullopt for anything else (a branch name, build metadata).
std::optional<ReleaseVersion> parse_version(std::string_view text);

// A range is the leading numbers of a version: "0" (0.x.y), "0.1" (0.1.x) or "0.1.2". A version is in it
// when those numbers match; its prereleases count too ("0.1.0-alpha.2" is in "0.1").
bool valid_version_range(std::string_view range);
bool version_in_range(std::string_view version, std::string_view range);

// Parse git tags output (e.g. from git ls-remote --tags --refs) into semantic versions, newest first.
// Releases only, unless `include_prereleases`.
std::vector<std::string> parse_release_tags(std::string_view ls_remote_output, bool include_prereleases = false);

// Fetch release tags from remote repository.
std::vector<std::string> list_releases(const ToolkitEntry& entry, const ToolkitLog& log = {});

// An installed toolkit is outdated when a newer release exists, or, installed from main ("dev"), when main moved on.
bool toolkit_outdated(const ToolkitEntry& entry, const std::filesystem::path& repo_root = {});

// Locate the Paperboat repository root (containing toolkits.toml and engine/toolkit).
std::filesystem::path find_repo_root(const std::filesystem::path& hint = {});

// Where toolkits are installed: <root>/engine/toolkit in a checkout (compiled into the engine), else
// $PAPERBOAT_HOME/toolkits, where each is built as a plugin against the installed SDK (pb#354).
std::filesystem::path toolkits_dir(const std::filesystem::path& root);
std::filesystem::path toolkit_dir(const std::filesystem::path& root, std::string_view name);

// The installed SDK next to this binary: <prefix>/lib*/cmake/Paperboat, or empty in a build tree (pb#177).
std::filesystem::path sdk_dir();

// Parse toolkits.toml text or file. Throws DescriptorError on invalid TOML or missing fields.
std::vector<ToolkitEntry> parse_toolkits_manifest(std::string_view toml_text);
std::vector<ToolkitEntry> load_toolkits_manifest(const std::filesystem::path& manifest_path);

// Toolkit names required by a backend folder (from toolkit_requirements.toml, toolkits.toml,
// or descriptor.toml imports/templates matching known external toolkits).
std::vector<std::string> load_toolkit_requirements(const std::filesystem::path& backend_dir,
                                                   const std::vector<ToolkitEntry>& known = {});

// All toolkits from toolkits.toml with `installed` and `required` populated.
std::vector<ToolkitEntry> list_toolkits(const std::filesystem::path& repo_root = {},
                                        const std::filesystem::path& backend_dir = {},
                                        bool fetch_versions = false);

// Protocol JSON array for hello.toolkits / toolkits.status.
std::string toolkits_json(const std::vector<ToolkitEntry>& list,
                          std::string_view busy_name = {},
                          std::string_view busy_state = {});

// Install or switch version of an optional toolkit in engine/toolkit/<name>.
// `source` is a git URL or a local folder; when empty, PAPERBOAT_TOOLKIT_REPO_<NAME> /
// PAPERBOAT_TOOLKIT_REPO, then the manifest's suggested `repo`, are tried. A local folder is
// copied; a URL is cloned. The manifest is never used at launch.
ToolkitResult install_toolkit(std::string_view name,
                              std::string_view version = {},
                              const std::filesystem::path& repo_root = {},
                              const HostBuild& host = {},
                              const ToolkitLog& log = {},
                              std::string_view source = {});

ToolkitResult uninstall_toolkit(std::string_view name,
                                const std::filesystem::path& repo_root = {},
                                const HostBuild& host = {},
                                const ToolkitLog& log = {});

// Remote release versions/tags for an optional toolkit.
std::vector<std::string> list_toolkit_versions(std::string_view name,
                                               const std::filesystem::path& repo_root = {});

// Inspect blocks, station objects, and dashboard instruments of a toolkit or package.
ToolkitInspectResult inspect_toolkit(std::string_view name,
                                     const Registry& registry = builtins(),
                                     const std::filesystem::path& repo_root = {});
std::string toolkit_inspect_json(const ToolkitInspectResult& res);
std::string toolkit_inspect_text(const ToolkitInspectResult& res);

}  // namespace paperboat::toolkit
