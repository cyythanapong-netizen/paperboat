#include "paperboat/toolkit/plugin.hpp"

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <set>

#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/platform.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace paperboat::toolkit {
namespace {

namespace fs = std::filesystem;

#if defined(_WIN32)
constexpr std::string_view kPrefix = "paperboat_plugin_", kExt = ".dll";
constexpr char kSep = ';';
#elif defined(__APPLE__)
constexpr std::string_view kPrefix = "libpaperboat_plugin_", kExt = ".dylib";
constexpr char kSep = ':';
#else
constexpr std::string_view kPrefix = "libpaperboat_plugin_", kExt = ".so";
constexpr char kSep = ':';
#endif

using AbiFn = const char* (*)();
using RegisterFn = void (*)(Registry&);

// Opens a library and finds the plugin's two symbols; the handle is never closed (the registry's
// templates call into it).
bool open(const fs::path& path, AbiFn& abi, RegisterFn& reg, std::string& error) {
#if defined(_WIN32)
  HMODULE h = LoadLibraryW(path.wstring().c_str());
  if (!h) {
    error = "cannot load (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  abi = reinterpret_cast<AbiFn>(GetProcAddress(h, "paperboat_plugin_abi"));
  reg = reinterpret_cast<RegisterFn>(GetProcAddress(h, "paperboat_plugin_register"));
#else
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    const char* e = dlerror();
    error = e ? e : "cannot load";
    return false;
  }
  abi = reinterpret_cast<AbiFn>(dlsym(h, "paperboat_plugin_abi"));
  reg = reinterpret_cast<RegisterFn>(dlsym(h, "paperboat_plugin_register"));
#endif
  if (!abi || !reg) {
    error = "not a paperboat plugin (no paperboat_plugin_abi / paperboat_plugin_register)";
    return false;
  }
  return true;
}

// The plugin files under `dir`, up to three levels down (<dir>/<name>/lib/<file>).
void find(const fs::path& dir, std::vector<fs::path>& out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (it.depth() > 2) it.disable_recursion_pending();
    const std::string file = it->path().filename().string();
    if (file.starts_with(kPrefix) && file.ends_with(kExt) && it->is_regular_file(ec)) out.push_back(it->path());
  }
}

}  // namespace

std::vector<fs::path> plugin_dirs() {
  std::vector<fs::path> dirs;
  if (const char* v = std::getenv("PAPERBOAT_TOOLKIT_PATH"); v && *v) {
    std::string s = v;
    for (std::size_t from = 0, to; from <= s.size(); from = to + 1) {
      to = s.find(kSep, from);
      if (to == std::string::npos) to = s.size();
      if (to > from) dirs.emplace_back(s.substr(from, to - from));
    }
  }
  dirs.push_back(apps_dir().parent_path() / "toolkits");
  if (const fs::path exe = platform::exe_path(); !exe.empty()) {
    std::error_code ec;
    for (fs::path p = exe.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
      if (fs::is_regular_file(p / "CMakeCache.txt", ec)) {  // a build tree: <build>/toolkit/<name>/
        dirs.push_back(p / "toolkit");
        break;
      }
    }
    dirs.push_back(exe.parent_path().parent_path() / "lib" / "paperboat" / "plugins");
  }
  return dirs;
}

std::vector<PluginLoad> load_plugins(Registry& registry, const std::vector<fs::path>& dirs) {
  std::vector<fs::path> files;
  for (const auto& d : dirs) find(d, files);
  std::vector<PluginLoad> out;
  std::set<std::string> seen;
  const auto have = registry.packages();
  for (const auto& path : files) {
    PluginLoad r;
    r.path = path;
    r.name = path.stem().string().substr(kPrefix.size());
    if (!seen.insert(r.name).second) continue;  // an earlier directory has it
    if (std::ranges::find(have, r.name) != have.end()) continue;  // linked in, or registered already
    AbiFn abi = nullptr;
    RegisterFn reg = nullptr;
    if (!open(path, abi, reg, r.message)) {
      out.push_back(std::move(r));
      continue;
    }
    if (const std::string theirs = abi(); theirs != PAPERBOAT_PLUGIN_ABI) {
      r.message = "built for " + theirs + ", this is " + PAPERBOAT_PLUGIN_ABI + "; reinstall the toolkit";
      out.push_back(std::move(r));
      continue;
    }
    try {
      reg(registry);
      r.ok = true;
    } catch (const std::exception& e) {
      r.message = std::string("failed to register: ") + e.what();
    }
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace paperboat::toolkit
