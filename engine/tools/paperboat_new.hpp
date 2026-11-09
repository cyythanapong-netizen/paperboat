#pragma once

// `paperboat new`: scaffold a minimal backend (C++ or Python) that passes `paperboat check` as
// it is. The files are templates/<language>/ (new_templates.cmake embeds them in the binary).

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace paperboat::cli {

// One embedded template: its language ("cpp", "python"), its path in the new backend
// ("domain/blocks.hpp"; "gitignore" is written as .gitignore) and its text with @name@-style
// placeholders.
struct TemplateFile {
  std::string_view language;
  std::string_view path;
  std::string_view content;
};
std::span<const TemplateFile> templates();  // generated: new_templates.cpp

struct NewOptions {
  std::string name;               // the backend's name and domain: lowercase, digits, '-', '_'
  std::string language = "cpp";   // "cpp" or "python"
  std::filesystem::path dir;      // where to write it; empty: ./<name>
};

struct NewResult {
  bool ok = false;
  std::string error;
  std::filesystem::path dir;                // the backend's folder
  std::vector<std::filesystem::path> files;  // written, relative to dir
};

// The port a new backend serves on (backend.toml's port), clear of the examples' 8765-8781.
inline constexpr int kNewBackendPort = 8790;

// Empty if `name` is a valid backend name, else what is wrong with it.
std::string check_backend_name(std::string_view name);

// Writes the backend. Refuses a name that isn't valid, an unknown language, and a folder that
// exists and isn't empty (nothing is ever overwritten).
NewResult new_backend(const NewOptions& options);

// The command: paperboat new <name> [--cpp | --python] [--dir <path>]
int cmd_new(const std::vector<std::string_view>& args);

}  // namespace paperboat::cli
