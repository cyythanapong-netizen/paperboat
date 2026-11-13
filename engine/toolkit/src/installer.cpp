#include "paperboat/toolkit/installer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/platform.hpp"

namespace paperboat::toolkit {
namespace {

using nlohmann::json;
namespace fs = std::filesystem;

bool has_repo_markers(const fs::path& dir) {
  return !dir.empty() && fs::is_regular_file(dir / "toolkits.toml") && fs::is_directory(dir / "engine" / "toolkit");
}

fs::path walk_up(fs::path p) {
  std::error_code ec;
  p = fs::weakly_canonical(p, ec);
  while (!p.empty()) {
    if (has_repo_markers(p)) return p;
    // A paperboat-examples checkout sits next to paperboat, not inside it: a backend's
    // descriptor or executable lives under <workspace>/paperboat-examples/NN-…, so the
    // repository root is a sibling, not an ancestor. Check it at every step.
    if (has_repo_markers(p / "paperboat")) return p / "paperboat";
    const fs::path parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  return {};
}

#ifdef _WIN32
// cmd.exe: double quotes, a quote inside as \".
std::string shell_quote(std::string_view arg) {
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') out += "\\\"";
    else out += c;
  }
  out += "\"";
  return out;
}
FILE* open_pipe(const std::string& cmd) { return _popen(("\"" + cmd + "\"").c_str(), "r"); }  // cmd /c drops one outer pair of quotes
int close_pipe(FILE* f) { return _pclose(f); }
constexpr const char* kCommandPrefix = "";
constexpr const char* kCd = "cd /d ";
#else
std::string shell_quote(std::string_view arg) {
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}
FILE* open_pipe(const std::string& cmd) { return popen(cmd.c_str(), "r"); }
int close_pipe(FILE* f) { return pclose(f); }
constexpr const char* kCommandPrefix = "TMPDIR=/tmp XDG_RUNTIME_DIR=/tmp ";
constexpr const char* kCd = "cd ";
#endif

std::string trim_line(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}

int run_cmd(const std::vector<std::string>& args,
            const fs::path& cwd,
            const ToolkitLog& log,
            std::string* last_line = nullptr,
            std::string* all_output = nullptr) {
  std::string cmd = kCommandPrefix;
  if (!cwd.empty()) cmd += kCd + shell_quote(cwd.string()) + " && ";
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) cmd += ' ';
    cmd += shell_quote(args[i]);
  }
  cmd += " 2>&1";

  FILE* pipe = open_pipe(cmd);
  if (!pipe) {
    if (last_line) *last_line = "cannot start command";
    return -1;
  }
  std::array<char, 512> buf{};
  std::string line;
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    if (all_output) *all_output += buf.data();
    line = trim_line(buf.data());
    if (!line.empty()) {
      if (last_line) *last_line = line;
      if (log) log("info", line);
    }
  }
  const int status = close_pipe(pipe);
  return status;
}

// An explicit `source` wins. Otherwise the per-toolkit env override, then the general override,
// then the manifest's suggested repo. The last is only a convenience for an explicit install;
// check/launch never call this.
std::string resolve_repo_url(const ToolkitEntry& entry, std::string_view source) {
  if (!source.empty()) return std::string(source);
  std::string upper = entry.name;
  std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  const std::string env_key = "PAPERBOAT_TOOLKIT_REPO_" + upper;
  if (const char* v = std::getenv(env_key.c_str()); v && *v) return v;
  if (const char* v = std::getenv("PAPERBOAT_TOOLKIT_REPO"); v && *v) return v;
  return entry.repo;
}

const ToolkitEntry* find_entry(const std::vector<ToolkitEntry>& list, std::string_view name) {
  auto it = std::ranges::find(list, name, &ToolkitEntry::name);
  return it == list.end() ? nullptr : &*it;
}

#ifdef _WIN32
// The .exe files under `build` (not its dependencies' or CMake's own) that a running process holds,
// renamed to <name>.exe.old; returns (program, where it went). A stale .old from an earlier
// install is removed first, unless that one still runs too.
std::vector<std::pair<fs::path, fs::path>> move_running_programs_aside(const fs::path& build) {
  std::vector<std::pair<fs::path, fs::path>> moved;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(build, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const fs::path& p = it->path();
    if (it->is_directory(ec)) {
      const std::string name = p.filename().string();
      if (name == "_deps" || name == "CMakeFiles") it.disable_recursion_pending();
      continue;
    }
    if (p.extension() != ".exe") continue;
    // a program that runs is open without FILE_SHARE_WRITE: opening it for writing fails
    if (std::FILE* f = nullptr; _wfopen_s(&f, p.c_str(), L"r+b") == 0 && f) {
      std::fclose(f);
      continue;
    }
    fs::path aside = p;
    aside += ".old";
    std::error_code rm;
    fs::remove(aside, rm);
    if (std::error_code mv; fs::rename(p, aside, mv), !mv) moved.emplace_back(p, aside);
  }
  return moved;
}
#endif

ToolkitResult rebuild_host(const HostBuild& host, const fs::path& root, const ToolkitLog& log) {
  if (host.skip_rebuild) return {true, "rebuild skipped"};
  fs::path src = host.source_dir;
  fs::path bld = host.build_dir;
  if (src.empty() && bld.empty()) {
    if (fs::is_regular_file(root / "engine" / "CMakeLists.txt")) {
      src = root / "engine";
      bld = src / "build";
    } else {
      return {true, "no host build configured"};
    }
  }
  if (!src.empty() && !fs::is_directory(src)) return {false, "source directory not found at " + src.string()};
  const auto build_guard = lock_build();  // one build at a time: a restart's rebuild may be waiting
  if (log) log("info", "reconfiguring build at " + bld.string() + "…");
  const std::string cmake = platform::cmake_command(bld);
  std::string last;
  // Only a build folder (a running backend's): reconfigure it from its own cache.
  const std::vector<std::string> configure = src.empty() ? std::vector<std::string>{cmake, bld.string()}
                                                         : std::vector<std::string>{cmake, "-S", src.string(), "-B", bld.string()};
  if (run_cmd(configure, root, log, &last) != 0) {
    return {false, "cmake configure failed" + (last.empty() ? "" : ": " + last)};
  }
  if (log) log("info", "building…");
  std::vector<std::string> build = {cmake, "--build", bld.string()};
#ifdef _WIN32
  // Programs of this build that are running (this paperboat.exe, the workbench the console
  // started) cannot be overwritten on Windows, but they can be renamed: move them aside so the
  // link can write the new ones. Build the configuration this paperboat came from (Visual
  // Studio's default is Debug).
  std::error_code ec;
  const fs::path self = platform::exe_path();
  const auto moved = move_running_programs_aside(bld);
  std::string config = "Release";
  if (!self.empty()) {
    const std::string dir = self.parent_path().filename().string();
    if (dir == "Debug" || dir == "Release" || dir == "RelWithDebInfo" || dir == "MinSizeRel") config = dir;
  }
  build.insert(build.end(), {"--config", config});
#endif
  if (run_cmd(build, root, log, &last) != 0) {
#ifdef _WIN32
    // whatever was not linked again: put the program we moved aside back
    for (const auto& [program, aside] : moved)
      if (!fs::exists(program, ec)) fs::rename(aside, program, ec);
#endif
    return {false, "cmake build failed" + (last.empty() ? "" : ": " + last)};
  }
  return {true, "build succeeded"};
}

// Extract the toolkit folder from a cloned repository into `target_dir`.
ToolkitResult materialize_toolkit(const fs::path& clone_dir, std::string_view name, const fs::path& target_dir) {
  fs::path src_dir;
  if (fs::is_regular_file(clone_dir / "toolkit.toml")) {
    src_dir = clone_dir;
  } else if (fs::is_regular_file(clone_dir / "engine" / "toolkit" / name / "toolkit.toml")) {
    src_dir = clone_dir / "engine" / "toolkit" / name;
  } else {
    return {false, "repository does not contain toolkit.toml"};
  }

  std::error_code ec;
  fs::remove_all(target_dir, ec);
  fs::create_directories(target_dir.parent_path(), ec);
  if (src_dir == clone_dir) {
    fs::rename(clone_dir, target_dir, ec);
    if (ec) {
      ec.clear();
      fs::copy(clone_dir, target_dir, fs::copy_options::recursive, ec);
      fs::remove_all(clone_dir);
    }
  } else {
    fs::copy(src_dir, target_dir, fs::copy_options::recursive, ec);
    // A toolkit repository may keep its examples at the root; install them next to the
    // toolkit so the CLI finds them under engine/toolkit/<name>/examples.
    std::error_code examples_ec;
    if (!ec && fs::is_directory(clone_dir / "examples", examples_ec)) {
      fs::copy(clone_dir / "examples", target_dir / "examples", fs::copy_options::recursive, ec);
    }
    fs::remove_all(clone_dir);
  }
  if (ec || !fs::is_regular_file(target_dir / "toolkit.toml")) {
    return {false, "failed to install toolkit files into " + target_dir.string()};
  }
  return {true, {}};
}

// Marker file helpers
std::string read_marker_version(const fs::path& toolkit_dir) {
  const fs::path marker = toolkit_dir / ".paperboat-toolkit.json";
  if (!fs::is_regular_file(marker)) return "";
  try {
    std::ifstream in(marker);
    const json j = json::parse(in);
    return j.value("version", "");
  } catch (...) {
    return "";
  }
}

void write_marker(const fs::path& toolkit_dir, std::string_view version, std::string_view ref, std::string_view commit) {
  const fs::path marker = toolkit_dir / ".paperboat-toolkit.json";
  try {
    json j = {{"version", std::string(version)}, {"ref", std::string(ref)}, {"commit", std::string(commit)}};
    std::ofstream out(marker);
    out << j.dump(2) << "\n";
  } catch (...) {
  }
}

// SemVer §11: dot-separated identifiers, numeric ones compared as numbers and before alphanumeric ones.
std::strong_ordering compare_prerelease(std::string_view a, std::string_view b) {
  const auto numeric = [](std::string_view s) { return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; }); };
  while (!a.empty() && !b.empty()) {
    const auto da = a.find('.'), db = b.find('.');
    const std::string_view x = a.substr(0, da), y = b.substr(0, db);
    a = da == std::string_view::npos ? std::string_view{} : a.substr(da + 1);
    b = db == std::string_view::npos ? std::string_view{} : b.substr(db + 1);
    const bool nx = numeric(x), ny = numeric(y);
    std::strong_ordering c = std::strong_ordering::equal;
    if (nx && ny) c = x.size() != y.size() ? x.size() <=> y.size() : x.compare(y) <=> 0;
    else if (nx != ny) c = nx ? std::strong_ordering::less : std::strong_ordering::greater;
    else c = x.compare(y) <=> 0;
    if (c != 0) return c;
  }
  return a.empty() == b.empty() ? std::strong_ordering::equal : a.empty() ? std::strong_ordering::less : std::strong_ordering::greater;
}

// 1 to `max` dot-separated numbers ("0", "0.1", "0.1.2"); empty when it is not that.
std::vector<int> leading_numbers(std::string_view s, std::size_t max) {
  std::vector<int> out;
  while (true) {
    const auto dot = s.find('.');
    const std::string_view part = s.substr(0, dot);
    int n = 0;
    const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), n);
    if (part.empty() || ec != std::errc{} || end != part.data() + part.size() || out.size() == max) return {};
    out.push_back(n);
    if (dot == std::string_view::npos) return out;
    s.remove_prefix(dot + 1);
  }
}

}  // namespace

int run_command(const std::vector<std::string>& args, const fs::path& cwd, const ToolkitLog& log,
                std::string* last_line, std::string* all_output) {
  return run_cmd(args, cwd, log, last_line, all_output);
}

std::strong_ordering ReleaseVersion::operator<=>(const ReleaseVersion& o) const {
  if (auto c = std::tie(major, minor, patch) <=> std::tie(o.major, o.minor, o.patch); c != 0) return c;
  if (prerelease.empty() || o.prerelease.empty())  // a release is newer than its prereleases
    return prerelease.empty() == o.prerelease.empty() ? std::strong_ordering::equal
           : prerelease.empty()                       ? std::strong_ordering::greater
                                                      : std::strong_ordering::less;
  return compare_prerelease(prerelease, o.prerelease);
}

std::string ReleaseVersion::str() const {
  std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
  return prerelease.empty() ? s : s + "-" + prerelease;
}

std::optional<ReleaseVersion> parse_version(std::string_view text) {
  if (text.starts_with('v')) text.remove_prefix(1);
  const auto dash = text.find('-');
  const auto core = leading_numbers(text.substr(0, dash), 3);
  if (core.size() != 3) return std::nullopt;
  ReleaseVersion v{core[0], core[1], core[2], {}};
  if (dash != std::string_view::npos) {
    const std::string_view pre = text.substr(dash + 1);
    static const std::regex ident(R"([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)");
    if (!std::regex_match(pre.begin(), pre.end(), ident)) return std::nullopt;
    v.prerelease = std::string(pre);
  }
  return v;
}

// One cmake build at a time in this process: two that touch the same build folder at once fail on
// each other's files (MSBuild's .lib and .pdb), which is how a project broke during an install and
// a restart build that overlapped. Take this before configuring or building.
std::unique_lock<std::mutex> lock_build() {
  static std::mutex builds;
  return std::unique_lock<std::mutex>(builds);
}

bool valid_version_range(std::string_view range) { return !leading_numbers(range, 3).empty(); }

bool version_in_range(std::string_view version, std::string_view range) {
  const auto v = parse_version(version);
  const auto r = leading_numbers(range, 3);
  if (!v || r.empty()) return false;
  const int parts[] = {v->major, v->minor, v->patch};
  return std::equal(r.begin(), r.end(), parts);
}

std::vector<std::string> parse_release_tags(std::string_view ls_remote_output, bool include_prereleases) {
  std::istringstream stream{std::string(ls_remote_output)};
  std::vector<ReleaseVersion> parsed;
  for (std::string line; std::getline(stream, line);) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    const auto at = line.find("refs/tags/");
    if (at == std::string::npos) continue;
    std::string_view tag = std::string_view(line).substr(at + 10);
    if (tag.ends_with("^{}")) tag.remove_suffix(3);
    const auto v = parse_version(tag);
    if (!v || (!include_prereleases && !v->prerelease.empty())) continue;
    if (std::ranges::find(parsed, *v) == parsed.end()) parsed.push_back(*v);
  }
  std::ranges::sort(parsed, std::greater<>{});  // newest first
  std::vector<std::string> out;
  out.reserve(parsed.size());
  for (const auto& p : parsed) out.push_back(p.str());
  return out;
}

std::vector<std::string> list_releases(const ToolkitEntry& entry, const ToolkitLog& log) {
  const std::string url = resolve_repo_url(entry, {});
  std::string out;
  std::string last;
  if (run_cmd({"git", "ls-remote", "--tags", "--refs", url}, {}, log, &last, &out) != 0) {
    if (log) log("warn", "could not list tags from " + url);
    return {};
  }
  return parse_release_tags(out);
}

// An installed package: <prefix>/bin/paperboat with the registries in <prefix>/share/paperboat.
fs::path install_root() {
  const fs::path exe = platform::exe_path();
  if (exe.empty()) return {};
  const fs::path share = exe.parent_path().parent_path() / "share" / "paperboat";
  return fs::is_regular_file(share / "toolkits.toml") ? share : fs::path{};
}

fs::path find_repo_root(const fs::path& hint) {
  if (const char* env = std::getenv("PAPERBOAT_ROOT"); env && *env) {
    if (auto p = walk_up(env); !p.empty()) return p;
  }
  if (!hint.empty()) {
    if (auto p = walk_up(hint); !p.empty()) return p;
  }
  if (auto p = walk_up(fs::current_path()); !p.empty()) return p;
  // A binary built in a checkout (engine/build/...) finds it from where it runs.
  if (const fs::path exe = platform::exe_path(); !exe.empty()) {
    if (auto p = walk_up(exe.parent_path()); !p.empty()) return p;
  }
  return install_root();
}

fs::path toolkits_dir(const fs::path& root) {
  // A checkout builds its toolkits into the engine; an installed paperboat keeps them per user and
  // builds each one as a plugin against the installed SDK (pb#354): the root is then this binary's own
  // share/paperboat.
  if (const fs::path installed = install_root(); !installed.empty() && !root.empty()) {
    std::error_code ec;
    if (fs::equivalent(root, installed, ec)) return apps_dir().parent_path() / "toolkits";
  }
  return root / "engine" / "toolkit";
}

fs::path toolkit_dir(const fs::path& root, std::string_view name) { return toolkits_dir(root) / std::string(name); }

fs::path sdk_dir() {
  const fs::path exe = platform::exe_path();
  if (exe.empty()) return {};
  const fs::path prefix = exe.parent_path().parent_path();
  std::error_code ec;
  for (const char* lib : {"lib", "lib64"}) {
    if (const fs::path dir = prefix / lib / "cmake" / "Paperboat"; fs::is_regular_file(dir / "PaperboatConfig.cmake", ec)) return dir;
  }
  // Debian puts libraries in lib/<multiarch>.
  for (const auto& entry : fs::directory_iterator(prefix / "lib", ec)) {
    if (const fs::path dir = entry.path() / "cmake" / "Paperboat"; fs::is_regular_file(dir / "PaperboatConfig.cmake", ec)) return dir;
  }
  return {};
}

std::vector<ToolkitEntry> parse_toolkits_manifest(std::string_view toml_text) {
  toml::table doc;
  try {
    doc = toml::parse(toml_text);
  } catch (const toml::parse_error& err) {
    throw DescriptorError("toolkits.toml: " + std::string(err.description()));
  }
  std::vector<ToolkitEntry> out;
  const auto* arr = doc["toolkits"].as_array();
  if (!arr) return out;
  for (std::size_t i = 0; i < arr->size(); ++i) {
    const auto* tbl = (*arr)[i].as_table();
    if (!tbl) throw DescriptorError("toolkits[" + std::to_string(i) + "] must be a table");
    ToolkitEntry e;
    e.name = (*tbl)["name"].value_or(std::string{});
    e.repo = (*tbl)["repo"].value_or(std::string{});
    e.description = (*tbl)["description"].value_or(std::string{});
    e.optional = (*tbl)["optional"].value_or(true);
    e.engine = (*tbl)["engine"].value_or(std::string{});
    e.prefix = (*tbl)["prefix"].value_or(std::string{});
    if (const auto* blocks = (*tbl)["blocks"].as_array()) {
      for (const auto& b : *blocks) {
        if (auto v = b.value<std::string>()) e.blocks.push_back(*v);
      }
    }
    // Another optional toolkit this one links: installing this one installs that first.
    if (const auto* needed = (*tbl)["requires"].as_array()) {
      for (const auto& r : *needed) {
        if (auto v = r.value<std::string>()) e.needs.push_back(*v);
      }
    }
    // `repo` is a suggested source only: a user may install the toolkit from any repository
    // or local folder. The manifest is a catalog, not an installer.
    if (e.name.empty()) {
      throw DescriptorError("toolkits[" + std::to_string(i) + "]: needs name");
    }
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<ToolkitEntry> load_toolkits_manifest(const fs::path& manifest_path) {
  std::ifstream in(manifest_path);
  if (!in) throw DescriptorError("cannot open " + manifest_path.string());
  std::stringstream buffer;
  buffer << in.rdbuf();
  return parse_toolkits_manifest(buffer.str());
}

std::vector<std::string> load_toolkit_requirements(const fs::path& backend_dir,
                                                   const std::vector<ToolkitEntry>& known) {
  if (backend_dir.empty()) return {};
  std::set<std::string> known_names;
  for (const auto& k : known) {
    if (!k.builtin) known_names.insert(k.name);
  }

  std::vector<std::string> reqs;
  auto add = [&](const std::string& name) {
    if (name.empty() || name == "builtin") return;
    if (!known_names.empty() && !known_names.contains(name)) return;
    if (std::ranges::find(reqs, name) == reqs.end()) reqs.push_back(name);
  };

  for (const char* fname : {"toolkit_requirements.toml"}) {
    const fs::path p = backend_dir / fname;
    if (!fs::is_regular_file(p)) continue;
    try {
      const auto doc = toml::parse_file(p.string());
      if (const auto* arr = doc["toolkits"].as_array()) {
        for (const auto& item : *arr) {
          if (const auto s = item.value<std::string>()) add(*s);
          else if (const auto* t = item.as_table()) add((*t)["name"].value_or(std::string{}));
        }
      }
      if (const auto* arr = doc["required"].as_array()) {
        for (const auto& item : *arr) {
          if (const auto s = item.value<std::string>()) add(*s);
        }
      }
    } catch (...) {
      // Ignore malformed optional requirement files.
    }
  }

  const fs::path desc = backend_dir / "descriptor.toml";
  if (fs::is_regular_file(desc)) {
    try {
      const auto doc = toml::parse_file(desc.string());
      if (const auto* imports = doc["import"].as_array()) {
        for (const auto& item : *imports) {
          if (const auto s = item.value<std::string>()) add(*s);
        }
      }
      for (const char* section : {"blocks", "objects"}) {
        if (const auto* list = doc[section].as_array()) {
          for (const auto& item : *list) {
            const auto* tbl = item.as_table();
            if (!tbl) continue;
            const std::string use = (*tbl)["use"].value_or(std::string{});
            const auto slash = use.find('/');
            if (slash != std::string::npos) add(use.substr(0, slash));
          }
        }
      }
    } catch (...) {
      // Descriptor syntax errors are reported by load_descriptor.
    }
  }
  return reqs;
}

std::vector<ToolkitEntry> list_toolkits(const fs::path& repo_root,
                                        const fs::path& backend_dir,
                                        bool fetch_versions) {
  std::string pkg_list;
  for (const auto& p : builtins().packages()) {
    if (!pkg_list.empty()) pkg_list += ", ";
    pkg_list += p;
  }
  ToolkitEntry builtin_entry;
  builtin_entry.name = "builtin";
  builtin_entry.builtin = true;
  builtin_entry.optional = false;
  builtin_entry.installed = true;
  builtin_entry.description = "Paperboat standard toolkit packages (" + pkg_list + ")";

  const fs::path root = repo_root.empty() ? find_repo_root(backend_dir) : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) {
    return {std::move(builtin_entry)};
  }
  auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const auto reqs = load_toolkit_requirements(backend_dir, entries);
  for (auto& e : entries) {
    const fs::path target = toolkit_dir(root, e.name);
    e.installed = fs::is_regular_file(target / "toolkit.toml");
    e.required = std::ranges::find(reqs, e.name) != reqs.end();
    if (e.installed) {
      e.version = read_marker_version(target);
      if (e.version.empty()) {
        try {
          const auto doc = toml::parse_file((target / "toolkit.toml").string());
          if (const auto v = doc["version"].value<std::string>()) {
            e.version = *v;
          }
        } catch (...) {
        }
      }
    }
    if (fetch_versions) {
      e.versions = list_releases(e);
      e.default_version = !e.versions.empty() ? e.versions.front() : "dev";
    }
  }
  entries.insert(entries.begin(), std::move(builtin_entry));
  return entries;
}

std::string toolkits_json(const std::vector<ToolkitEntry>& list,
                          std::string_view busy_name,
                          std::string_view busy_state) {
  json arr = json::array();
  for (const auto& e : list) {
    json item = {{"name", e.name}, {"repo", e.repo}, {"installed", e.installed}, {"builtin", e.builtin}};
    if (!e.description.empty()) item["description"] = e.description;
    if (e.required) item["required"] = true;
    if (!e.version.empty()) item["version"] = e.version;
    if (!e.versions.empty()) item["versions"] = e.versions;
    if (!e.default_version.empty()) item["default"] = e.default_version;
    if (!e.blocks.empty()) item["blocks"] = e.blocks;
    if (!busy_name.empty() && busy_name == e.name && !busy_state.empty()) item["busy"] = busy_state;
    arr.push_back(std::move(item));
  }
  return arr.dump();
}

// One toolkit's files on disk: the repository cloned (or the folder copied) into
// engine/toolkit/<name>. No build: install_toolkit() builds once after the whole set.
ToolkitResult install_files(const ToolkitEntry& entry, std::string_view requested_version, std::string_view source,
                            const fs::path& root, const ToolkitLog& log) {
  // The source is explicit (--repo/--path), an env override, or the manifest's suggestion.
  const std::string source_text = resolve_repo_url(entry, source);
  if (source_text.empty()) {
    std::string upper = entry.name;
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return {false, "no source for toolkit \"" + entry.name +
                       "\": pass --repo <url> or --path <dir>, or set PAPERBOAT_TOOLKIT_REPO_" + upper};
  }
  const fs::path source_path(source_text);
  const bool local = fs::is_directory(source_path);
  const fs::path local_source = local ? fs::weakly_canonical(source_path) : fs::path{};

  // Resolve target version and git ref. No version asked: stay on the channel it is on (main stays
  // main); "latest" moves it to releases.
  std::string version = std::string(requested_version);
  if (version.empty() && read_marker_version(toolkit_dir(root, entry.name)) == "dev") version = "dev";
  if (version == "latest") version.clear();
  std::string git_ref;

  if (local) {
    version = version.empty() ? "local" : version;
    git_ref = "local";
  } else if (version.empty()) {
    ToolkitEntry probe = entry;
    probe.repo = source_text;
    const auto available = list_releases(probe, log);
    if (!available.empty()) {
      version = available.front();
      git_ref = "v" + version;
    } else {
      version = "dev";
      git_ref = "main";
    }
  } else if (version == "dev" || version == "main") {
    version = "dev";
    git_ref = "main";
  } else {
    // Validate tag exists or format
    git_ref = version.starts_with('v') ? version : "v" + version;
    version = version.starts_with('v') ? version.substr(1) : version;
  }

  if (log) log("info", "installing toolkit \"" + entry.name + "\" version " + version + " (" + git_ref + ") from " + source_text + "…");

  const fs::path target_dir = toolkit_dir(root, entry.name);
  const fs::path stage_dir = toolkits_dir(root) / ".toolkit-cache" / ("stage-" + entry.name);
  std::error_code ec;
  fs::remove_all(stage_dir, ec);
  fs::create_directories(stage_dir.parent_path(), ec);

  std::string commit;
  if (local) {
    fs::copy(local_source, stage_dir, fs::copy_options::recursive, ec);
    if (ec) return {false, "cannot copy " + local_source.string() + ": " + ec.message()};
    commit = "local";
  } else {
    std::string last;
    std::vector<std::string> clone_cmd = {"git", "-c", "advice.detachedHead=false", "clone", "--depth", "1"};
    if (!git_ref.empty() && git_ref != "HEAD" && git_ref != "main") {  // "main": the default branch, whatever its name
      clone_cmd.push_back("--branch");
      clone_cmd.push_back(git_ref);
    }
    clone_cmd.push_back(source_text);
    clone_cmd.push_back(stage_dir.string());

    int clone_rc = run_cmd(clone_cmd, root, log, &last);
    if (clone_rc != 0 && (git_ref == "main" || git_ref == "dev")) {
      // Try master or default clone
      fs::remove_all(stage_dir, ec);
      clone_rc = run_cmd({"git", "-c", "advice.detachedHead=false", "clone", "--depth", "1", source_text, stage_dir.string()}, root, log, &last);
    } else if (clone_rc != 0 && git_ref.starts_with('v')) {
      // Try without 'v' prefix
      std::string fallback_ref = git_ref.substr(1);
      fs::remove_all(stage_dir, ec);
      clone_rc = run_cmd({"git", "-c", "advice.detachedHead=false", "clone", "--depth", "1", "--branch", fallback_ref, source_text, stage_dir.string()}, root, log, &last);
      if (clone_rc == 0) git_ref = fallback_ref;
    }
    if (clone_rc != 0) {
      fs::remove_all(stage_dir, ec);
      return {false, "failed to clone " + source_text + " at ref " + git_ref + (last.empty() ? "" : ": " + last)};
    }

    run_cmd({"git", "-C", stage_dir.string(), "rev-parse", "HEAD"}, root, {}, nullptr, &commit);
    commit = trim_line(commit);
  }

  // Backup existing install if present
  const fs::path prev_backup = toolkits_dir(root) / ".toolkit-cache" / ("prev-" + entry.name);
  fs::remove_all(prev_backup, ec);
  if (fs::exists(target_dir)) {
    fs::rename(target_dir, prev_backup, ec);
  }

  const auto mat = materialize_toolkit(stage_dir, entry.name, target_dir);
  if (!mat.ok) {
    std::error_code restore_ec;
    fs::remove_all(target_dir, restore_ec);  // a partial copy; rename cannot replace it
    if (fs::exists(prev_backup)) fs::rename(prev_backup, target_dir, restore_ec);
    return mat;
  }
  fs::remove_all(prev_backup, ec);

  write_marker(target_dir, version, git_ref, commit);
  return {true, {}};
}

bool toolkit_outdated(const ToolkitEntry& entry, const fs::path& repo_root) {
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  json m;
  try {
    std::ifstream in(toolkit_dir(root, entry.name) / ".paperboat-toolkit.json");
    m = json::parse(in);
  } catch (...) {
    return false;
  }
  const std::string version = m.value("version", "");
  if (version == "local") return false;
  if (version != "dev") {
    const auto releases = list_releases(entry);
    const auto have = parse_version(version), best = releases.empty() ? std::nullopt : parse_version(releases.front());
    return have && best && *best > *have;
  }
  std::string out;  // a dev install tracks the default branch: outdated when it moved on
  if (run_cmd({"git", "ls-remote", resolve_repo_url(entry, {}), "HEAD"}, root, {}, nullptr, &out) != 0) return false;
  return !out.empty() && out.rfind(m.value("commit", ""), 0) != 0;
}

namespace {

// An installed paperboat (no engine sources): build `entry` as a plugin, libpaperboat_plugin_<name> in
// <toolkit>/lib, against the installed SDK, with the toolkits it requires built in beside it. Also
// writes <toolkit>/lib/paperboat-toolkit.cmake, which PaperboatConfig.cmake includes so a C++ backend
// can link paperboat::toolkit_<name> (pb#354).
ToolkitResult build_plugin(const ToolkitEntry& entry, const std::vector<ToolkitEntry>& entries, const fs::path& root,
                           const ToolkitLog& log) {
  const fs::path sdk = sdk_dir();
  if (sdk.empty()) return {false, "no paperboat SDK next to this binary (lib/cmake/Paperboat): reinstall paperboat"};
  // The toolkit and everything it requires, requirements first.
  std::vector<std::string> chain;
  std::function<void(const std::string&)> add = [&](const std::string& n) {
    if (std::ranges::find(chain, n) != chain.end()) return;
    if (const ToolkitEntry* e = find_entry(entries, n))
      for (const auto& req : e->needs) add(req);
    chain.push_back(n);
  };
  add(entry.name);

  const fs::path dir = toolkit_dir(root, entry.name);
  const fs::path project = toolkits_dir(root) / ".build" / entry.name;
  std::error_code ec;
  fs::create_directories(project, ec);
  const auto quoted = [](const fs::path& p) { return "\"" + p.generic_string() + "\""; };
  {
    std::ofstream cm(project / "CMakeLists.txt");
    cm << "# Generated by `paperboat toolkit install " << entry.name << "`: the toolkit as a plugin.\n"
       << "cmake_minimum_required(VERSION 3.24)\n"
       << "project(paperboat_plugin_" << entry.name << " C CXX)\n"
       << "set(PAPERBOAT_NO_INSTALLED_TOOLKITS ON)  # built from their sources below\n"
       << "find_package(Paperboat REQUIRED CONFIG)\n"
       << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n"
       << "set(PAPERBOAT_BUILD_TESTS OFF)\n"
       << "set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)  # a toolkit's dependencies, seen from here\n"
       << "add_library(paperboat_warnings INTERFACE)  # what a toolkit links inside the engine build\n"
       << "target_link_libraries(paperboat_warnings INTERFACE paperboat::warnings)\n";
    for (const auto& n : chain) cm << "add_subdirectory(" << quoted(toolkit_dir(root, n)) << " toolkit/" << n << ")\n";
    std::string reqs;
    for (const auto& n : chain)
      if (n != entry.name) reqs += ";paperboat::toolkit_" + n;
    const std::string tk = "paperboat_toolkit_" + entry.name, plugin = "paperboat_plugin_" + entry.name;
    cm << "add_library(" << plugin << " SHARED plugin.cpp)\n"
       << "target_link_libraries(" << plugin << " PRIVATE " << tk << ")\n"
       << "set_target_properties(" << plugin << " PROPERTIES LIBRARY_OUTPUT_DIRECTORY " << quoted(dir / "lib")
       << " RUNTIME_OUTPUT_DIRECTORY " << quoted(dir / "lib") << ")\n"
       // The package a backend links (PaperboatConfig includes it): the plugin, the toolkit's headers, and the
       // imported libraries it uses (OpenCV for opencv), which a backend including its header needs too.
       << "set(_incs \"\")\nset(_libs \"\")\n"
       << "get_target_property(_ll " << tk << " LINK_LIBRARIES)\nget_target_property(_il " << tk << " INTERFACE_LINK_LIBRARIES)\n"
       << "foreach(_l IN LISTS _ll _il)\n"
       << "  string(REGEX REPLACE \"^\\\\$<LINK_ONLY:(.*)>$\" \"\\\\1\" _l \"${_l}\")\n"
       << "  if(TARGET \"${_l}\" AND NOT _l MATCHES \"^paperboat\")\n"
       << "    get_target_property(_imported \"${_l}\" IMPORTED)\n    get_target_property(_type \"${_l}\" TYPE)\n"
       << "    if(_imported)\n      list(APPEND _incs \"$<TARGET_PROPERTY:${_l},INTERFACE_INCLUDE_DIRECTORIES>\")\n"
       << "      if(NOT _type STREQUAL \"INTERFACE_LIBRARY\")\n        list(APPEND _libs \"$<TARGET_FILE:${_l}>\")\n      endif()\n"
       << "    endif()\n  endif()\nendforeach()\n"
       << "list(REMOVE_DUPLICATES _incs)\nlist(REMOVE_DUPLICATES _libs)\nlist(JOIN _incs \";\" _incs)\nlist(JOIN _libs \";\" _libs)\n"
       << "file(GENERATE OUTPUT " << quoted(dir / "lib" / "paperboat-toolkit.cmake") << " CONTENT \"if(NOT TARGET paperboat::toolkit_"
       << entry.name << ")\n  add_library(paperboat::toolkit_" << entry.name << " SHARED IMPORTED)\n"
       << "  set_target_properties(paperboat::toolkit_" << entry.name << " PROPERTIES IMPORTED_LOCATION \\\"$<TARGET_FILE:" << plugin << ">\\\"\n"
       << "    INTERFACE_INCLUDE_DIRECTORIES \\\"" << (dir / "include").generic_string() << ";${_incs}\\\"\n"
       << "    INTERFACE_LINK_LIBRARIES \\\"paperboat::toolkit" << reqs << ";${_libs}\\\")\nendif()\n\")\n";
    std::ofstream(project / "plugin.cpp") << "#include <paperboat/toolkit/plugin.hpp>\n#include <paperboat/toolkit/" << entry.name
                                         << ".hpp>\nPAPERBOAT_TOOLKIT_PLUGIN(paperboat::toolkit::" << entry.name
                                         << "::register_" << entry.name << ")\n";
  }
  const fs::path bld = project / "build";
  const std::string cmake = platform::cmake_command(bld);
  std::string last;
  if (log) log("info", "building toolkit \"" + entry.name + "\" as a plugin…");
  std::vector<std::string> configure = {cmake, "-S", project.string(), "-B", bld.string(), "-DCMAKE_BUILD_TYPE=Release",
                                        "-DPaperboat_DIR=" + sdk.string()};
  if (!fs::exists(bld / "CMakeCache.txt") && run_cmd({"ninja", "--version"}, project, {}) == 0) configure.push_back("-GNinja");
  // Quiet unless it fails; then the end of its output, which names the error.
  const auto tail = [](const std::string& out) {
    std::size_t from = out.size();
    for (int lines = 0; from > 0 && lines < 15; ++lines) {
      const auto nl = out.rfind('\n', from - 1);
      from = nl == std::string::npos ? 0 : nl;
    }
    return out.substr(from);
  };
  std::string out;
  if (run_cmd(configure, project, {}, &last, &out) != 0) return {false, "cmake configure failed:\n" + tail(out)};
  if (run_cmd({cmake, "--build", bld.string(), "--parallel"}, project, {}, &last, &out) != 0)
    return {false, "cmake build failed:\n" + tail(out)};

  return {true, "built"};
}

// A toolkit's web part (web/index.ts(x), or <block>/web/index.ts(x)) bundled into <toolkit>/web-dist as
// one ES module that `paperboat start` serves and the web app imports at startup, with React left to
// the app (pb#353). Needs Node's npx; without it the blocks work and only their instruments are missing.
void bundle_web(const ToolkitEntry& entry, const fs::path& root, const ToolkitLog& log) {
  const fs::path dir = toolkit_dir(root, entry.name);
  std::error_code ec;
  std::vector<fs::path> parts;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string n = it->path().filename().string();
    if (it->is_directory(ec) && (n == "node_modules" || n == "lib" || n == "web-dist" || n.starts_with("."))) {
      it.disable_recursion_pending();
    } else if ((n == "index.ts" || n == "index.tsx") && it->path().parent_path().filename() == "web") {
      parts.push_back(it->path());
    }
  }
  fs::remove_all(dir / "web-dist", ec);
  if (parts.empty()) return;
  const fs::path entry_file = dir / ".web-entry.ts";
  {
    std::ofstream out(entry_file);
    for (std::size_t i = 0; i < parts.size(); ++i)
      out << "import * as m" << i << " from \"./" << fs::relative(parts[i], dir).generic_string() << "\";\n";
    out << "export function register(host: unknown) {\n  for (const m of [";
    for (std::size_t i = 0; i < parts.size(); ++i) out << (i ? ", " : "") << "m" << i;
    out << "] as { register?: (h: unknown) => void }[]) m.register?.(host);\n}\n";
  }
  std::string out;
  const int rc = run_cmd({"npx", "--yes", "esbuild@0.25.12", entry_file.string(), "--bundle", "--format=esm", "--jsx=automatic",
                          "--minify", "--log-level=error", "--external:react", "--external:react/jsx-runtime",
                          "--external:react-dom", "--outfile=" + (dir / "web-dist" / "index.js").string()},
                         dir, {}, nullptr, &out);
  fs::remove(entry_file, ec);
  if (rc != 0 && log)
    log("warn", "toolkit \"" + entry.name + "\": its web part did not bundle (needs Node's npx); its blocks work, its instruments will not show: " + trim_line(out));
}

}  // namespace

ToolkitResult install_toolkit(std::string_view name,
                              std::string_view requested_version,
                              const fs::path& repo_root,
                              const HostBuild& host,
                              const ToolkitLog& log,
                              std::string_view source) {
  if (name == "builtin") {
    return {false, "cannot install built-in toolkit"};
  }
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) {
    return {false, "cannot find toolkits.toml in repository root"};
  }
  const auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const ToolkitEntry* entry = find_entry(entries, name);
  if (!entry) return {false, "unknown toolkit \"" + std::string(name) + "\""};
  if (entry->builtin) return {false, "cannot install built-in toolkit"};
  // A checkout compiles toolkits into its engine; an installed paperboat builds each as a plugin (pb#354).
  const bool checkout = toolkits_dir(root) == root / "engine" / "toolkit";
#ifdef _WIN32
  // A plugin needs the engine as shared libraries; the Windows build is static for now (#222).
  if (!checkout && host.source_dir.empty())
    return {false, "toolkits install as plugins on Linux and macOS; on Windows they need paperboat built from source for now (#222)"};
#endif

  // The toolkit and what it requires, each requirement first: control cannot be built without
  // optimization, and asking for that build step by hand is a trap.
  const auto installed = [&](const ToolkitEntry& e) {
    return fs::is_regular_file(toolkit_dir(root, e.name) / "toolkit.toml");
  };
  std::vector<const ToolkitEntry*> order;
  std::set<std::string> seen;
  std::string error;
  std::function<void(const ToolkitEntry&)> add = [&](const ToolkitEntry& e) {
    if (!error.empty() || seen.contains(e.name)) return;
    seen.insert(e.name);
    for (const auto& req : e.needs) {
      const ToolkitEntry* dependency = find_entry(entries, req);
      if (!dependency) {
        error = "toolkit \"" + e.name + "\" requires \"" + req + "\", which toolkits.toml does not list";
        return;
      }
      if (!dependency->builtin && !installed(*dependency)) add(*dependency);
      if (!error.empty()) return;
    }
    if (e.name != name && installed(e)) return;  // already on disk: leave it as it is
    order.push_back(&e);
  };
  add(*entry);
  if (!error.empty()) return {false, error};

  std::string names;
  for (const ToolkitEntry* e : order) {
    const bool asked = e->name == name;
    const auto res = install_files(*e, asked ? requested_version : std::string_view{}, asked ? source : std::string_view{}, root, log);
    if (!res.ok) return res;
    if (!names.empty()) names += ", ";
    names += e->name;
  }

  if (checkout || !host.source_dir.empty()) {
    const auto rb = rebuild_host(host, root, log);
    if (!rb.ok) {
      return {false, "toolkit" + std::string(order.size() > 1 ? "s " : " ") + names + " installed but rebuild failed: " + rb.message};
    }
  } else if (!host.skip_rebuild) {
    // Each one installed now, then every installed toolkit built on one of them (control carries
    // optimization's code), so none keeps an old copy.
    std::set<std::string> rebuilt;
    std::vector<const ToolkitEntry*> todo(order.begin(), order.end());
    for (std::size_t i = 0; i < todo.size(); ++i) {
      if (!rebuilt.insert(todo[i]->name).second) continue;
      if (const auto r = build_plugin(*todo[i], entries, root, log); !r.ok)
        return {false, "toolkit \"" + todo[i]->name + "\" installed but its plugin did not build: " + r.message};
      bundle_web(*todo[i], root, log);
      for (const auto& other : entries)
        if (installed(other) && std::ranges::find(other.needs, todo[i]->name) != other.needs.end()) todo.push_back(&other);
    }
  }

  const std::string msg = "installed toolkit" + std::string(order.size() > 1 ? "s " : " ") + names + "; restart backend to load changes";
  if (log) log("info", msg);
  return {true, msg};
}

ToolkitResult uninstall_toolkit(std::string_view name,
                                const fs::path& repo_root,
                                const HostBuild& host,
                                const ToolkitLog& log) {
  if (name == "builtin") {
    return {false, "cannot uninstall built-in toolkit"};
  }
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) {
    return {false, "cannot find toolkits.toml in repository root"};
  }
  const auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const ToolkitEntry* entry = find_entry(entries, name);
  if (!entry) return {false, "unknown toolkit \"" + std::string(name) + "\""};
  if (entry->builtin) return {false, "cannot uninstall built-in toolkit"};

  const fs::path target_dir = toolkit_dir(root, entry->name);
  if (!fs::exists(target_dir)) {
    if (log) log("info", "toolkit \"" + entry->name + "\" is not installed");
    return {true, "toolkit \"" + entry->name + "\" is not installed"};
  }

  // One that another installed toolkit requires goes last: the build would fail without it.
  for (const auto& other : entries) {
    if (other.name == entry->name || other.builtin) continue;
    if (std::ranges::find(other.needs, entry->name) == other.needs.end()) continue;
    if (fs::is_regular_file(toolkit_dir(root, other.name) / "toolkit.toml"))
      return {false, "toolkit \"" + entry->name + "\" is required by \"" + other.name + "\": uninstall that first"};
  }

  if (log) log("info", "uninstalling toolkit \"" + entry->name + "\"…");
  std::error_code ec;
  fs::remove_all(target_dir, ec);
  if (ec) return {false, "failed to remove " + target_dir.string() + ": " + ec.message()};
  fs::remove_all(toolkits_dir(root) / ".build" / entry->name, ec);  // its plugin build, on an installed paperboat

  const auto rb = rebuild_host(host, root, log);
  if (!rb.ok) return rb;

  const std::string msg = "uninstalled toolkit \"" + entry->name + "\"";
  if (log) log("info", msg);
  return {true, msg};
}

std::vector<std::string> list_toolkit_versions(std::string_view name,
                                               const fs::path& repo_root) {
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) return {};
  const auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const ToolkitEntry* entry = find_entry(entries, name);
  if (!entry) return {};

  const std::string repo_url = resolve_repo_url(*entry, {});
  if (repo_url.empty()) return {};

  std::vector<std::string> versions;
  std::string out;
  if (run_cmd({"git", "ls-remote", "--tags", repo_url}, {}, {}, nullptr, &out) != 0) return {};
  std::istringstream lines(out);
  for (std::string raw; std::getline(lines, raw);) {
    const std::string line = trim_line(raw);
    const std::string marker = "refs/tags/";
    auto pos = line.find(marker);
    if (pos == std::string::npos) continue;
    std::string tag = line.substr(pos + marker.size());
    if (tag.ends_with("^{}")) tag.resize(tag.size() - 3);
    if (!tag.empty() && std::ranges::find(versions, tag) == versions.end()) {
      versions.push_back(std::move(tag));
    }
  }
  std::ranges::sort(versions);
  return versions;
}

ToolkitInspectResult inspect_toolkit(std::string_view name,
                                     const Registry& registry,
                                     const fs::path& repo_root) {
  ToolkitInspectResult res;
  auto collect_package = [&](std::string_view pkg) {
    for (const auto* st : registry.objects(pkg)) {
      res.station_blocks.push_back({st->name, st->def.label, st->def.description});
    }
    for (const auto* t : registry.package(pkg)) {
      if (t->abstract) continue;
      if (!t->def.instrument.empty()) {
        res.dashboard_instruments.push_back({t->name, t->def.label, t->def.instrument, t->def.description});
      } else {
        res.flow_blocks.push_back({t->name, t->def.label, t->def.description});
      }
    }
  };

  if (name == "builtin") {
    res.name = "builtin";
    res.builtin = true;
    res.installed = true;
    res.found = true;
    for (const auto& pkg : builtins().packages()) {
      collect_package(pkg);
    }
    return res;
  }

  const auto builtin_pkgs = builtins().packages();
  if (std::ranges::find(builtin_pkgs, name) != builtin_pkgs.end()) {
    res.name = std::string(name);
    res.builtin = true;
    res.installed = true;
    res.found = true;
    collect_package(name);
    return res;
  }

  const auto all_toolkits = list_toolkits(repo_root);
  if (const auto* entry = find_entry(all_toolkits, name)) {
    res.name = entry->name;
    res.builtin = entry->builtin;
    res.installed = entry->installed || !registry.package(name).empty() || !registry.objects(name).empty();
    res.found = true;
    collect_package(name);
    return res;
  }

  const auto reg_pkgs = registry.packages();
  if (std::ranges::find(reg_pkgs, name) != reg_pkgs.end()) {
    res.name = std::string(name);
    res.builtin = false;
    res.installed = true;
    res.found = true;
    collect_package(name);
    return res;
  }

  return res;
}

std::string toolkit_inspect_json(const ToolkitInspectResult& res) {
  json st_arr = json::array();
  for (const auto& b : res.station_blocks) {
    st_arr.push_back({{"type", b.type}, {"label", b.label}, {"description", b.description}});
  }
  json fl_arr = json::array();
  for (const auto& b : res.flow_blocks) {
    fl_arr.push_back({{"type", b.type}, {"label", b.label}, {"description", b.description}});
  }
  json in_arr = json::array();
  for (const auto& i : res.dashboard_instruments) {
    in_arr.push_back({{"type", i.type}, {"label", i.label}, {"instrument", i.instrument}, {"description", i.description}});
  }
  json doc = {
      {"name", res.name},
      {"builtin", res.builtin},
      {"installed", res.installed},
      {"station_blocks", std::move(st_arr)},
      {"flow_blocks", std::move(fl_arr)},
      {"dashboard_instruments", std::move(in_arr)},
  };
  return doc.dump(2);
}

std::string toolkit_inspect_text(const ToolkitInspectResult& res) {
  std::ostringstream out;
  out << "Toolkit: " << res.name << " ("
      << (res.builtin ? "built-in" : (res.installed ? "installed" : "not installed")) << ")\n";

  out << "\nStation blocks (" << res.station_blocks.size() << "):\n";
  if (res.station_blocks.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto& b : res.station_blocks) {
      out << "  " << b.type << "\t" << b.label;
      if (!b.description.empty()) out << "\t" << b.description;
      out << "\n";
    }
  }

  out << "\nFlow blocks (" << res.flow_blocks.size() << "):\n";
  if (res.flow_blocks.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto& b : res.flow_blocks) {
      out << "  " << b.type << "\t" << b.label;
      if (!b.description.empty()) out << "\t" << b.description;
      out << "\n";
    }
  }

  out << "\nDashboard instruments (" << res.dashboard_instruments.size() << "):\n";
  if (res.dashboard_instruments.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto& i : res.dashboard_instruments) {
      out << "  " << i.type << "\t" << i.label << " [" << i.instrument << "]";
      if (!i.description.empty()) out << "\t" << i.description;
      out << "\n";
    }
  }

  return out.str();
}

}  // namespace paperboat::toolkit
