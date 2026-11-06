#pragma once

// Optional external toolkits: declared in toolkits.toml at the repository root and installed on
// demand into engine/toolkit/<name>. Backends may declare required toolkits in
// toolkit_requirements.toml (or import/use them in descriptor.toml).

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace paperboat::toolkit {

struct ToolkitEntry {
  std::string name;
  std::string repo;
  std::string description;
  bool optional = true;
  std::string engine;
  bool installed = false;
  bool required = false;
};

struct ToolkitResult {
  bool ok = false;
  std::string message;
};

using ToolkitLog = std::function<void(std::string_view level, std::string_view text)>;

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
                                        const std::filesystem::path& backend_dir = {});

// Protocol JSON array for hello.toolkits / toolkits.status.
std::string toolkits_json(const std::vector<ToolkitEntry>& list,
                          std::string_view busy_name = {},
                          std::string_view busy_state = {});

// Install, update, or uninstall an optional toolkit in engine/toolkit/<name>.
// When `rebuild` is true, also reconfigures and builds engine/build via CMake.
ToolkitResult install_toolkit(std::string_view name,
                              const std::filesystem::path& repo_root = {},
                              const ToolkitLog& log = {},
                              bool rebuild = true);

ToolkitResult update_toolkit(std::string_view name,
                             const std::filesystem::path& repo_root = {},
                             const ToolkitLog& log = {},
                             bool rebuild = true);

ToolkitResult uninstall_toolkit(std::string_view name,
                                const std::filesystem::path& repo_root = {},
                                const ToolkitLog& log = {},
                                bool rebuild = true);

}  // namespace paperboat::toolkit
