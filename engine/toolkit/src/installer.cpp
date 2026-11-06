#include "paperboat/toolkit/installer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "paperboat/toolkit/descriptor.hpp"

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
    const fs::path parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  return {};
}

std::string shell_quote(std::string_view arg) {
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

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
  std::string cmd = "TMPDIR=/tmp XDG_RUNTIME_DIR=/tmp ";
  if (!cwd.empty()) cmd += "cd " + shell_quote(cwd.string()) + " && ";
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) cmd += ' ';
    cmd += shell_quote(args[i]);
  }
  cmd += " 2>&1";

  FILE* pipe = popen(cmd.c_str(), "r");
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
  const int status = pclose(pipe);
  return status;
}

std::string resolve_repo_url(const ToolkitEntry& entry) {
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
  if (!fs::is_directory(src)) return {false, "source directory not found at " + src.string()};
  if (log) log("info", "reconfiguring build at " + bld.string() + "…");
  std::string last;
  if (run_cmd({"cmake", "-S", src.string(), "-B", bld.string()}, root, log, &last) != 0) {
    return {false, "cmake configure failed" + (last.empty() ? "" : ": " + last)};
  }
  if (log) log("info", "building…");
  if (run_cmd({"cmake", "--build", bld.string()}, root, log, &last) != 0) {
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

struct VersionTriple {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string raw;

  auto operator<=>(const VersionTriple& o) const {
    if (major != o.major) return major <=> o.major;
    if (minor != o.minor) return minor <=> o.minor;
    return patch <=> o.patch;
  }
  bool operator==(const VersionTriple& o) const = default;
};

}  // namespace

std::vector<std::string> parse_release_tags(std::string_view ls_remote_output) {
  std::istringstream stream{std::string(ls_remote_output)};
  std::string line;
  const std::regex tag_regex(R"(refs/tags/v?(\d+)\.(\d+)\.(\d+)(?:\^\{\})?$)");
  std::vector<VersionTriple> parsed;

  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_search(line, match, tag_regex)) {
      int maj = std::stoi(match[1].str());
      int min = std::stoi(match[2].str());
      int pat = std::stoi(match[3].str());
      std::string ver = match[1].str() + "." + match[2].str() + "." + match[3].str();
      VersionTriple vt{maj, min, pat, ver};
      if (std::ranges::find(parsed, vt) == parsed.end()) {
        parsed.push_back(std::move(vt));
      }
    }
  }

  // Sort newest first
  std::ranges::sort(parsed, std::greater<VersionTriple>{});

  std::vector<std::string> out;
  out.reserve(parsed.size());
  for (const auto& p : parsed) out.push_back(p.raw);
  return out;
}

std::vector<std::string> list_releases(const ToolkitEntry& entry, const ToolkitLog& log) {
  const std::string url = resolve_repo_url(entry);
  std::string out;
  std::string last;
  if (run_cmd({"git", "ls-remote", "--tags", "--refs", url}, {}, log, &last, &out) != 0) {
    if (log) log("warn", "could not list tags from " + url);
    return {};
  }
  return parse_release_tags(out);
}

fs::path find_repo_root(const fs::path& hint) {
  if (const char* env = std::getenv("PAPERBOAT_ROOT"); env && *env) {
    if (auto p = walk_up(env); !p.empty()) return p;
  }
  if (!hint.empty()) {
    if (auto p = walk_up(hint); !p.empty()) return p;
  }
  if (auto p = walk_up(fs::current_path()); !p.empty()) return p;
#ifdef PAPERBOAT_SOURCE_DIR
  if (auto p = walk_up(fs::path(PAPERBOAT_SOURCE_DIR)); !p.empty()) return p;
#endif
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
    if (e.name.empty() || e.repo.empty()) {
      throw DescriptorError("toolkits[" + std::to_string(i) + "]: needs name and repo");
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
  for (const auto& k : known) known_names.insert(k.name);

  std::vector<std::string> reqs;
  auto add = [&](const std::string& name) {
    if (name.empty()) return;
    if (!known_names.empty() && !known_names.contains(name)) return;
    if (std::ranges::find(reqs, name) == reqs.end()) reqs.push_back(name);
  };

  for (const char* fname : {"toolkit_requirements.toml", "toolkits.toml"}) {
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
  const fs::path root = repo_root.empty() ? find_repo_root(backend_dir) : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) return {};
  auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const auto reqs = load_toolkit_requirements(backend_dir, entries);
  for (auto& e : entries) {
    const fs::path target = root / "engine" / "toolkit" / e.name;
    e.installed = fs::is_regular_file(target / "toolkit.toml");
    e.required = std::ranges::find(reqs, e.name) != reqs.end();
    if (e.installed) {
      e.version = read_marker_version(target);
      if (e.version.empty()) e.version = "unknown";
    }
    if (fetch_versions) {
      e.versions = list_releases(e);
      e.default_version = !e.versions.empty() ? e.versions.front() : "dev";
    }
  }
  return entries;
}

std::string toolkits_json(const std::vector<ToolkitEntry>& list,
                          std::string_view busy_name,
                          std::string_view busy_state) {
  json arr = json::array();
  for (const auto& e : list) {
    json item = {{"name", e.name}, {"repo", e.repo}, {"installed", e.installed}};
    if (!e.description.empty()) item["description"] = e.description;
    if (e.required) item["required"] = true;
    if (!e.version.empty()) item["version"] = e.version;
    if (!e.versions.empty()) item["versions"] = e.versions;
    if (!e.default_version.empty()) item["default"] = e.default_version;
    if (!busy_name.empty() && busy_name == e.name && !busy_state.empty()) item["busy"] = busy_state;
    arr.push_back(std::move(item));
  }
  return arr.dump();
}

ToolkitResult install_toolkit(std::string_view name,
                              std::string_view requested_version,
                              const fs::path& repo_root,
                              const HostBuild& host,
                              const ToolkitLog& log) {
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) {
    return {false, "cannot find toolkits.toml in repository root"};
  }
  const auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const ToolkitEntry* entry = find_entry(entries, name);
  if (!entry) return {false, "unknown toolkit \"" + std::string(name) + "\""};

  const std::string url = resolve_repo_url(*entry);

  // Resolve target version and git ref
  std::string version = std::string(requested_version);
  std::string git_ref;

  if (version.empty()) {
    const auto available = list_releases(*entry, log);
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

  if (log) log("info", "installing toolkit \"" + entry->name + "\" version " + version + " (" + git_ref + ") from " + url + "…");

  const fs::path target_dir = root / "engine" / "toolkit" / entry->name;
  const fs::path stage_dir = root / ".toolkit-cache" / ("stage-" + entry->name);
  std::error_code ec;
  fs::remove_all(stage_dir, ec);
  fs::create_directories(stage_dir.parent_path(), ec);

  std::string last;
  std::vector<std::string> clone_cmd = {"git", "clone", "--depth", "1"};
  if (!git_ref.empty() && git_ref != "HEAD") {
    clone_cmd.push_back("--branch");
    clone_cmd.push_back(git_ref);
  }
  clone_cmd.push_back(url);
  clone_cmd.push_back(stage_dir.string());

  int clone_rc = run_cmd(clone_cmd, root, log, &last);
  if (clone_rc != 0 && (git_ref == "main" || git_ref == "dev")) {
    // Try master or default clone
    fs::remove_all(stage_dir, ec);
    clone_rc = run_cmd({"git", "clone", "--depth", "1", url, stage_dir.string()}, root, log, &last);
  } else if (clone_rc != 0 && git_ref.starts_with('v')) {
    // Try without 'v' prefix
    std::string fallback_ref = git_ref.substr(1);
    fs::remove_all(stage_dir, ec);
    clone_rc = run_cmd({"git", "clone", "--depth", "1", "--branch", fallback_ref, url, stage_dir.string()}, root, log, &last);
    if (clone_rc == 0) git_ref = fallback_ref;
  }
  if (clone_rc != 0) {
    fs::remove_all(stage_dir, ec);
    return {false, "failed to clone " + url + " at ref " + git_ref + (last.empty() ? "" : ": " + last)};
  }

  std::string commit;
  run_cmd({"git", "-C", stage_dir.string(), "rev-parse", "HEAD"}, root, {}, nullptr, &commit);
  commit = trim_line(commit);

  // Backup existing install if present
  const fs::path prev_backup = root / ".toolkit-cache" / ("prev-" + entry->name);
  fs::remove_all(prev_backup, ec);
  if (fs::exists(target_dir)) {
    fs::rename(target_dir, prev_backup, ec);
  }

  const auto mat = materialize_toolkit(stage_dir, entry->name, target_dir);
  if (!mat.ok) {
    if (fs::exists(prev_backup)) fs::rename(prev_backup, target_dir, ec);
    return mat;
  }
  fs::remove_all(prev_backup, ec);

  write_marker(target_dir, version, git_ref, commit);

  const auto rb = rebuild_host(host, root, log);
  if (!rb.ok) {
    return {false, "toolkit installed but rebuild failed: " + rb.message};
  }

  const std::string msg = "installed toolkit \"" + entry->name + "\" (" + version + "); restart backend to load changes";
  if (log) log("info", msg);
  return {true, msg};
}

ToolkitResult uninstall_toolkit(std::string_view name,
                                const fs::path& repo_root,
                                const HostBuild& host,
                                const ToolkitLog& log) {
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  if (root.empty() || !fs::is_regular_file(root / "toolkits.toml")) {
    return {false, "cannot find toolkits.toml in repository root"};
  }
  const auto entries = load_toolkits_manifest(root / "toolkits.toml");
  const ToolkitEntry* entry = find_entry(entries, name);
  if (!entry) return {false, "unknown toolkit \"" + std::string(name) + "\""};

  const fs::path target_dir = root / "engine" / "toolkit" / entry->name;
  if (!fs::exists(target_dir)) {
    if (log) log("info", "toolkit \"" + entry->name + "\" is not installed");
    return {true, "toolkit \"" + entry->name + "\" is not installed"};
  }

  if (log) log("info", "uninstalling toolkit \"" + entry->name + "\"…");
  std::error_code ec;
  fs::remove_all(target_dir, ec);
  if (ec) return {false, "failed to remove " + target_dir.string() + ": " + ec.message()};

  const auto rb = rebuild_host(host, root, log);
  if (!rb.ok) return rb;

  const std::string msg = "uninstalled toolkit \"" + entry->name + "\"";
  if (log) log("info", msg);
  return {true, msg};
}

}  // namespace paperboat::toolkit
