#pragma once

// Optional external toolkits: declared in toolkits.toml at the repository root and installed on
// demand into engine/toolkit/<name>. Backends may declare required toolkits in
// toolkit_requirements.toml (or import/use them in descriptor.toml).

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit {

struct ToolkitEntry {
  std::string name;
  std::string repo;
  std::string description;
  bool optional = true;
  bool builtin = false;
  std::string engine;
  bool installed = false;
  bool required = false;
  std::string version;
  std::vector<std::string> versions;
  std::string default_version;
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

// Parse git tags output (e.g. from git ls-remote --tags --refs) into semantic version numbers, newest first.
std::vector<std::string> parse_release_tags(std::string_view ls_remote_output);

// Fetch release tags from remote repository.
std::vector<std::string> list_releases(const ToolkitEntry& entry, const ToolkitLog& log = {});

// Locate the Paperboat repository root (containing toolkits.toml and engine/toolkit).
std::filesystem::path find_repo_root(const std::filesystem::path& hint = {});

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
ToolkitResult install_toolkit(std::string_view name,
                              std::string_view version = {},
                              const std::filesystem::path& repo_root = {},
                              const HostBuild& host = {},
                              const ToolkitLog& log = {});

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
