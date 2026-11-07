#include "paperboat/toolkit/apps.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "paperboat/toolkit/descriptor.hpp"

namespace paperboat::toolkit {
namespace {

using nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kMarker = ".paperboat-app.json";

std::vector<std::string> strings(const toml::table& t, std::string_view key) {
  std::vector<std::string> out;
  if (const auto* arr = t[key].as_array())
    for (const auto& v : *arr)
      if (auto s = v.value<std::string>()) out.push_back(*s);
  return out;
}

std::vector<std::string> words(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream in(line);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

// The last few lines of a failed step's output, for the error message.
std::string tail(const std::string& output, int lines = 8) {
  std::size_t pos = output.size();
  while (pos > 0 && lines > 0) {
    pos = output.rfind('\n', pos - 1);
    if (pos == std::string::npos) return output;
    if (--lines == 0) break;
    if (pos == 0) return output;
  }
  return output.substr(pos + 1);
}

std::string trimmed(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// PAPERBOAT_APP_REPO_<NAME> (dashes as underscores) points one app at another repository, or a local one.
std::string repo_url(const AppEntry& e) {
  std::string key = "PAPERBOAT_APP_REPO_";
  for (char c : e.name) key += c == '-' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (const char* v = std::getenv(key.c_str()); v && *v) return v;
  return e.repo;
}

json read_marker(const fs::path& dir) {
  std::ifstream in(dir / kMarker);
  if (!in) return json::object();
  try {
    return json::parse(in);
  } catch (...) {
    return json::object();
  }
}

fs::path manifest_path(const fs::path& repo_root) {
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  return root.empty() ? fs::path{} : root / "apps.toml";
}

std::vector<AppEntry> load(const fs::path& repo_root, std::string& error) {
  const fs::path file = manifest_path(repo_root);
  if (file.empty() || !fs::is_regular_file(file)) {
    error = "cannot find apps.toml in the repository root";
    return {};
  }
  std::ifstream in(file);
  std::stringstream text;
  text << in.rdbuf();
  try {
    return parse_apps_manifest(text.str());
  } catch (const DescriptorError& e) {
    error = e.what();
    return {};
  }
}

const AppEntry* find(const std::vector<AppEntry>& list, std::string_view name) {
  auto it = std::ranges::find(list, name, &AppEntry::name);
  return it == list.end() ? nullptr : &*it;
}

// An app's build steps in `dir`, then a check that its output folder exists. Failure text has the step's output tail.
ToolkitResult run_build(const AppEntry& e, const fs::path& dir, const ToolkitLog& log) {
  for (const auto& step : e.build) {
    if (log) log("info", "$ " + step);
    std::string output, last;
    if (run_command(words(step), dir, {}, &last, &output) != 0)
      return {false, "\"" + step + "\" failed in " + dir.string() + ":\n" + trimmed(tail(output))};
  }
  if (!e.output.empty() && !fs::is_directory(dir / e.output))
    return {false, "the build of \"" + e.name + "\" made no " + e.output + "/"};
  return {true, "built"};
}

bool installed(std::string_view name) { return fs::is_regular_file(apps_dir() / std::string(name) / kMarker); }

}  // namespace

fs::path apps_dir() {
  if (const char* v = std::getenv("PAPERBOAT_HOME"); v && *v) return fs::path(v) / "apps";
  if (const char* v = std::getenv("XDG_DATA_HOME"); v && *v) return fs::path(v) / "paperboat" / "apps";
  const char* home = std::getenv("HOME");
  return fs::path(home && *home ? home : ".") / ".local" / "share" / "paperboat" / "apps";
}

std::vector<AppEntry> parse_apps_manifest(std::string_view toml_text) {
  toml::table doc;
  try {
    doc = toml::parse(toml_text);
  } catch (const toml::parse_error& err) {
    throw DescriptorError("apps.toml: " + std::string(err.description()));
  }
  std::vector<AppEntry> out;
  const auto* arr = doc["apps"].as_array();
  if (!arr) return out;
  for (std::size_t i = 0; i < arr->size(); ++i) {
    const auto* t = (*arr)[i].as_table();
    if (!t) throw DescriptorError("apps[" + std::to_string(i) + "] must be a table");
    AppEntry e;
    e.name = (*t)["name"].value_or(std::string{});
    e.repo = (*t)["repo"].value_or(std::string{});
    e.description = (*t)["description"].value_or(std::string{});
    e.output = (*t)["output"].value_or(std::string{});
    e.kind = (*t)["kind"].value_or(std::string{"program"});
    e.launch = (*t)["launch"].value_or(std::string{});
    e.port = (*t)["port"].value_or(0);
    if (e.kind != "program" && e.kind != "library")
      throw DescriptorError("apps[" + std::to_string(i) + "]: kind must be \"program\" or \"library\"");
    e.deps = strings(*t, "deps");
    e.build = strings(*t, "build");
    if (e.name.empty() || e.repo.empty()) throw DescriptorError("apps[" + std::to_string(i) + "]: needs name and repo");
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<AppEntry> list_apps(const fs::path& repo_root) {
  std::string error;
  auto list = load(repo_root, error);
  for (auto& e : list) {
    const json m = read_marker(apps_dir() / e.name);
    e.installed = !m.empty();
    e.version = m.value("version", "");
  }
  return list;
}

std::vector<std::string> list_app_versions(std::string_view name, const fs::path& repo_root) {
  std::string error;
  const auto list = load(repo_root, error);
  const AppEntry* e = find(list, name);
  if (!e) return {};
  std::string out;
  if (run_command({"git", "ls-remote", "--tags", "--refs", repo_url(*e)}, {}, {}, nullptr, &out) != 0) return {};
  return parse_release_tags(out);
}

ToolkitResult install_app(std::string_view name, std::string_view requested, const fs::path& repo_root,
                          const ToolkitLog& log) {
  std::string error;
  const auto list = load(repo_root, error);
  if (!error.empty()) return {false, error};
  const AppEntry* e = find(list, name);
  if (!e) return {false, "unknown app \"" + std::string(name) + "\""};

  for (const auto& dep : e->deps) {
    if (installed(dep)) continue;
    if (log) log("info", "\"" + e->name + "\" needs \"" + dep + "\"");
    if (auto r = install_app(dep, {}, repo_root, log); !r.ok) return r;
  }

  const std::string url = repo_url(*e);
  std::string version(requested);
  if (version.starts_with('v')) version.erase(0, 1);
  if (version.empty()) {
    const auto tags = list_app_versions(name, repo_root);
    version = tags.empty() ? "dev" : tags.front();
  }
  // Clone candidates in order; "" is the remote's default branch.
  const std::vector<std::string> refs = version == "dev" || version == "main"
                                            ? std::vector<std::string>{"main", ""}
                                            : std::vector<std::string>{"v" + version, version};
  if (version == "main") version = "dev";
  if (log) log("info", "fetching \"" + e->name + "\" " + version + " from " + url + "…");

  std::error_code ec;
  const fs::path dir = apps_dir() / e->name;
  const fs::path prev = apps_dir() / (".prev-" + e->name);
  fs::create_directories(apps_dir(), ec);
  fs::remove_all(prev, ec);
  if (fs::exists(dir)) fs::rename(dir, prev, ec);
  const auto restore = [&] {
    fs::remove_all(dir, ec);
    if (fs::exists(prev)) fs::rename(prev, dir, ec);
  };

  std::string last, ref;
  bool cloned = false;
  for (const auto& r : refs) {
    std::vector<std::string> cmd = {"git", "clone", "--depth", "1"};
    if (!r.empty()) cmd.insert(cmd.end(), {"--branch", r});
    cmd.insert(cmd.end(), {url, dir.string()});
    fs::remove_all(dir, ec);
    if (run_command(cmd, {}, {}, &last) == 0) {
      cloned = true;
      ref = r;
      break;
    }
  }
  if (!cloned) {
    restore();
    return {false, "cannot clone " + url + " at " + version + (last.empty() ? "" : ": " + last)};
  }
  std::string commit;
  run_command({"git", "-C", dir.string(), "rev-parse", "HEAD"}, {}, {}, nullptr, &commit);

  if (auto built = run_build(*e, dir, log); !built.ok) {
    restore();
    return built;
  }

  fs::remove_all(prev, ec);
  std::ofstream(dir / kMarker) << json{{"version", version}, {"ref", ref}, {"commit", trimmed(commit)},
                                       {"output", e->output}}.dump(2) << '\n';
  const std::string msg = "installed \"" + e->name + "\" " + version + " in " + dir.string();
  if (log) log("info", msg);
  return {true, msg};
}

ToolkitResult uninstall_app(std::string_view name, const fs::path& repo_root, const ToolkitLog& log) {
  std::string error;
  const auto list = load(repo_root, error);
  if (!error.empty()) return {false, error};
  if (!find(list, name)) return {false, "unknown app \"" + std::string(name) + "\""};
  if (!installed(name)) return {false, "\"" + std::string(name) + "\" is not installed"};
  for (const auto& other : list)
    if (installed(other.name) && std::ranges::find(other.deps, name) != other.deps.end())
      return {false, "\"" + std::string(name) + "\" is needed by \"" + other.name + "\"; uninstall that first"};
  std::error_code ec;
  fs::remove_all(apps_dir() / std::string(name), ec);
  if (ec) return {false, "cannot remove " + (apps_dir() / std::string(name)).string() + ": " + ec.message()};
  if (log) log("info", "uninstalled \"" + std::string(name) + "\"");
  return {true, "uninstalled"};
}

ToolkitResult build_app(std::string_view name, const fs::path& dir, const fs::path& repo_root, const ToolkitLog& log) {
  std::string error;
  const auto list = load(repo_root, error);
  if (!error.empty()) return {false, error};
  const AppEntry* e = find(list, name);
  if (!e) return {false, "unknown app \"" + std::string(name) + "\""};
  return run_build(*e, dir, log);
}

bool app_outdated(std::string_view name, const fs::path& repo_root) {
  std::string error;
  const auto list = load(repo_root, error);
  const AppEntry* e = find(list, name);
  const json m = read_marker(apps_dir() / std::string(name));
  if (!e || m.empty()) return false;
  const std::string version = m.value("version", "");
  if (version != "dev") {
    const auto tags = list_app_versions(name, repo_root);
    return !tags.empty() && tags.front() != version;
  }
  std::string out;  // a dev install tracks main: outdated when the remote's main moved on
  if (run_command({"git", "ls-remote", repo_url(*e), "main"}, {}, {}, nullptr, &out) != 0) return false;
  return !out.empty() && out.rfind(m.value("commit", ""), 0) != 0;
}

fs::path app_output(std::string_view name) {
  const fs::path dir = apps_dir() / std::string(name);
  const json m = read_marker(dir);
  if (m.empty()) return {};
  const std::string out = m.value("output", "");
  return out.empty() ? dir : dir / out;
}

}  // namespace paperboat::toolkit
