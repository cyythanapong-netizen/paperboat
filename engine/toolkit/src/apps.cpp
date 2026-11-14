#include "paperboat/toolkit/apps.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

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

// PAPERBOAT_APP_REPO_<NAME> (anything but letters and digits as underscores) points one app at another
// repository, or a local one.
std::string repo_env(std::string_view name) {
  std::string key = "PAPERBOAT_APP_REPO_";
  for (char c : name)
    key += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
  return key;
}

std::string repo_url(const AppEntry& e) {
  if (const char* v = std::getenv(repo_env(e.name).c_str()); v && *v) return v;
  return e.repo;
}

// git must never stop at a username prompt nobody can answer: a repository it cannot read fails at once.
void no_git_prompts() {
#ifdef _WIN32
  if (!std::getenv("GIT_TERMINAL_PROMPT")) _putenv_s("GIT_TERMINAL_PROMPT", "0");
  if (!std::getenv("GCM_INTERACTIVE")) _putenv_s("GCM_INTERACTIVE", "never");
#else
  setenv("GIT_TERMINAL_PROMPT", "0", 0);
  setenv("GCM_INTERACTIVE", "never", 0);
#endif
}

// Is `tool` an executable on PATH (or a path to one)?
bool on_path(const std::string& tool) {
  std::error_code ec;
  const auto runnable = [&](const fs::path& p) {
    if (!fs::is_regular_file(p, ec)) return false;
#ifdef _WIN32
    return true;
#else
    return access(p.c_str(), X_OK) == 0;
#endif
  };
#ifdef _WIN32
  const std::vector<std::string> exts = {"", ".exe", ".cmd", ".bat"};
  constexpr char sep = ';';
#else
  const std::vector<std::string> exts = {""};
  constexpr char sep = ':';
#endif
  if (tool.find('/') != std::string::npos) return runnable(tool);
  const char* path = std::getenv("PATH");
  std::istringstream dirs(path ? path : "");
  for (std::string dir; std::getline(dirs, dir, sep);)
    for (const auto& ext : exts)
      if (!dir.empty() && runnable(fs::path(dir) / (tool + ext))) return true;
  return false;
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

std::vector<AppEntry> load(const fs::path& repo_root, std::string& error,
                           std::map<std::string, std::string>* hints = nullptr) {
  no_git_prompts();  // every entry point loads the manifest before it runs git
  const fs::path file = manifest_path(repo_root);
  if (file.empty() || !fs::is_regular_file(file)) {
    error = "cannot find apps.toml in the repository root";
    return {};
  }
  std::ifstream in(file);
  std::stringstream text;
  text << in.rdbuf();
  try {
    if (hints) *hints = parse_tool_hints(text.str());
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

std::string installed_version(std::string_view name) {
  return read_marker(apps_dir() / std::string(name)).value("version", "");
}

bool prerelease(std::string_view version) { return version == "dev" || version.find('-') != std::string_view::npos; }

// The remote's release tags, prereleases included, newest first; `reachable` is false when git could not list them.
std::vector<std::string> remote_versions(const AppEntry& e, bool* reachable = nullptr) {
  std::string out;
  const bool ok = run_command({"git", "ls-remote", "--tags", "--refs", repo_url(e)}, {}, {}, nullptr, &out) == 0;
  if (reachable) *reachable = ok;
  return ok ? parse_release_tags(out, true) : std::vector<std::string>{};
}

// An app that needs another one within a range: a version of that one must stay inside it.
struct Constraint {
  std::string app;          // the app that needs it
  std::string app_version;  // at this version
  AppDep dep;
};

// The installed apps (but `except`) that need `name` with a range.
std::vector<Constraint> dependents(const std::vector<AppEntry>& list, std::string_view name, std::string_view except) {
  std::vector<Constraint> out;
  for (const auto& other : list) {
    if (other.name == except || !installed(other.name)) continue;
    for (const auto& d : other.deps)
      if (d.name == name && !d.range.empty()) out.push_back({other.name, installed_version(other.name), d});
  }
  return out;
}

// The newest of `versions` inside every constraint; prereleases only when one of the apps is a prerelease
// (with no constraints: releases only).
std::optional<std::string> newest_fitting(const std::vector<std::string>& versions, const std::vector<Constraint>& cs) {
  const bool pre = std::ranges::any_of(cs, [](const Constraint& c) { return prerelease(c.app_version); });
  for (const auto& v : versions) {
    if (!pre && v.find('-') != std::string::npos) continue;
    if (std::ranges::all_of(cs, [&](const Constraint& c) { return dep_satisfied(v, c.dep, c.app_version); })) return v;
  }
  return std::nullopt;
}

// "squero-studio-om" 0.1.0
std::string label(std::string_view name, std::string_view version) {
  return "\"" + std::string(name) + "\" " + std::string(version);
}

}  // namespace

fs::path apps_dir() {
  if (const char* v = std::getenv("PAPERBOAT_HOME"); v && *v) return fs::path(v) / "apps";
#ifdef _WIN32
  // %LOCALAPPDATA%\paperboat, where scripts/install.ps1 installs paperboat itself
  if (const char* v = std::getenv("LOCALAPPDATA"); v && *v) return fs::path(v) / "paperboat" / "apps";
#endif
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
    for (const auto& spec : strings(*t, "deps")) {
      const auto at = spec.find('@');
      AppDep d{spec.substr(0, at), at == std::string::npos ? std::string{} : spec.substr(at + 1)};
      if (d.name.empty() || (at != std::string::npos && !valid_version_range(d.range)))
        throw DescriptorError("apps[" + std::to_string(i) + "]: dep \"" + spec +
                              "\" must be a name or name@range (MAJOR, MAJOR.MINOR or MAJOR.MINOR.PATCH)");
      e.deps.push_back(std::move(d));
    }
    e.build = strings(*t, "build");
    e.prerequisites = strings(*t, "requires");
    for (const auto& r : e.prerequisites)
      if (r.empty() || r.front() == '|' || r.back() == '|' || r.find("||") != std::string::npos)
        throw DescriptorError("apps[" + std::to_string(i) + "]: requires \"" + r + "\" must be a tool or tool|tool|…");
    if (e.name.empty() || e.repo.empty()) throw DescriptorError("apps[" + std::to_string(i) + "]: needs name and repo");
    out.push_back(std::move(e));
  }
  return out;
}

std::map<std::string, std::string> parse_tool_hints(std::string_view toml_text) {
  std::map<std::string, std::string> out;
  try {
    const toml::table doc = toml::parse(toml_text);
    if (const auto* tools = doc["tools"].as_table())
      for (const auto& [k, v] : *tools)
        if (auto s = v.value<std::string>()) out[std::string(k.str())] = *s;
  } catch (const toml::parse_error& err) {
    throw DescriptorError("apps.toml: " + std::string(err.description()));
  }
  return out;
}

std::string missing_prerequisites(std::string_view name, const fs::path& repo_root) {
  std::string error;
  std::map<std::string, std::string> hints;
  const auto list = load(repo_root, error, &hints);
  // the app and every dep under it, each once
  std::vector<const AppEntry*> todo{find(list, name)}, seen;
  std::vector<std::string> missing;
  while (!todo.empty()) {
    const AppEntry* e = todo.back();
    todo.pop_back();
    if (!e || std::ranges::find(seen, e) != seen.end()) continue;
    seen.push_back(e);
    for (const auto& d : e->deps) todo.push_back(find(list, d.name));
    for (const auto& req : e->prerequisites) {
      std::vector<std::string> any;
      std::istringstream parts(req);
      for (std::string p; std::getline(parts, p, '|');) any.push_back(p);
      if (std::ranges::any_of(any, on_path)) continue;
      std::string what = any[0];  // "one of podman, docker or julia"
      if (any.size() > 1) {
        what = "one of ";
        for (std::size_t i = 0; i < any.size(); ++i) what += (i == 0 ? "" : i + 1 == any.size() ? " or " : ", ") + any[i];
      }
      std::string hint;
      for (const auto& a : any)
        if (auto it = hints.find(a); it != hints.end()) {
          hint = it->second;
          break;
        }
      if (auto it = hints.find(req); it != hints.end()) hint = it->second;
      const std::string line = what + " (\"" + e->name + "\")" + (hint.empty() ? "" : ": " + hint);
      if (std::ranges::find(missing, line) == missing.end()) missing.push_back(line);
    }
  }
  std::string out;
  for (const auto& m : missing) out += (out.empty() ? "" : "; ") + m;
  return out;
}

std::vector<AppEntry> list_apps(const fs::path& repo_root) {
  std::string error;
  auto list = load(repo_root, error);
  const fs::path root = repo_root.empty() ? find_repo_root() : repo_root;
  for (auto& e : list) {
    const json m = read_marker(apps_dir() / e.name);
    e.installed = !m.empty();
    e.version = m.value("version", "");
    std::error_code ec;
    e.checkout = !root.empty() && fs::is_directory(root.parent_path() / e.name, ec);
    if (const auto it = m.find("deps"); it != m.end() && it->is_object())
      for (const auto& [dep, v] : it->items())
        if (v.is_string()) e.built_with[dep] = v.get<std::string>();
  }
  return list;
}

std::vector<std::string> list_app_versions(std::string_view name, const fs::path& repo_root, bool include_prereleases) {
  std::string error;
  const auto list = load(repo_root, error);
  const AppEntry* e = find(list, name);
  if (!e) return {};
  std::string out;
  if (run_command({"git", "ls-remote", "--tags", "--refs", repo_url(*e)}, {}, {}, nullptr, &out) != 0) return {};
  return parse_release_tags(out, include_prereleases);
}

bool dep_satisfied(std::string_view dep_version, const AppDep& dep, std::string_view app_version) {
  if (dep.range.empty()) return true;
  if (dep_version == "dev") return app_version == "dev";
  return version_in_range(dep_version, dep.range);
}

namespace {

// install_app; `parent` is the app whose dep this is (its range is already met, and its marker is old).
ToolkitResult install(std::string_view name, std::string_view requested, const fs::path& repo_root,
                      const ToolkitLog& log, bool force, std::string_view parent) {
  std::string error;
  const auto list = load(repo_root, error);
  if (!error.empty()) return {false, error};
  const AppEntry* e = find(list, name);
  if (!e) return {false, "unknown app \"" + std::string(name) + "\""};
  // 0. what the machine lacks, before anything is fetched (once, for the app and all its deps)
  if (parent.empty())
    if (const auto missing = missing_prerequisites(e->name, repo_root); !missing.empty())
      return {false, "\"" + e->name + "\" needs what this machine lacks; install it, then run this again: " + missing};

  // 1. which version: the one asked for, else the newest release inside the ranges of the installed apps
  // that need this one, else main
  const auto needed_by = dependents(list, e->name, parent);
  std::string version(requested);
  if (version.starts_with('v')) version.erase(0, 1);
  // No version asked: stay on the channel it is on (main stays main); "latest" moves it to releases.
  if (version.empty() && parent.empty() && installed_version(e->name) == "dev") version = "dev";
  if (version == "latest") version.clear();
  bool reachable = true;
  const auto tags = remote_versions(*e, &reachable);
  if (version.empty()) version = newest_fitting(tags, needed_by).value_or("dev");
  if (version == "main") version = "dev";
  if (reachable && parse_version(version) && std::ranges::find(tags, version) == tags.end())
    return {false, "\"" + e->name + "\" has no release " + version + " (`paperboat pkg versions " + e->name + "` lists them)"};
  for (const auto& c : needed_by) {
    if (dep_satisfied(version, c.dep, c.app_version)) continue;
    const std::string why = label(e->name, version) + " is outside " + c.dep.range + ", the range " +
                            label(c.app, c.app_version) + " needs";
    if (!force) return {false, why + "; install a version in it, or pass --force"};
    if (log) log("warn", why + " (--force)");
  }

  // 2. deps, each paired with this version
  json built_with = json::object();
  for (const auto& dep : e->deps) {
    const AppEntry* d = find(list, dep.name);
    if (!d) return {false, "\"" + e->name + "\" needs \"" + dep.name + "\", which apps.toml does not declare"};
    const std::string have = installed_version(dep.name);
    if (!have.empty() && dep_satisfied(have, dep, version)) {
      if (log && !dep.range.empty()) log("info", label(dep.name, have) + " is installed and fits " + dep.spec());
      built_with[dep.name] = have;
      continue;
    }
    if (!have.empty()) {
      const std::string why = label(e->name, version) + " needs \"" + dep.spec() + "\", and " + have + " is installed";
      if (!force)
        return {false, why + "; reinstall it in range with `paperboat pkg install " + e->name +
                           (requested.empty() ? "" : "@" + version) + " --force`"};
      if (log) log("warn", why + "; reinstalling it (--force)");
    } else if (log) {
      log("info", "\"" + e->name + "\" needs \"" + dep.spec() + "\"");
    }
    std::string pick;  // empty: the dep's own default, as for a plain install
    if (!dep.range.empty()) {
      bool dep_reachable = true;
      auto cs = dependents(list, dep.name, e->name);
      cs.push_back({e->name, version, dep});
      if (auto v = newest_fitting(remote_versions(*d, &dep_reachable), cs)) {
        pick = *v;
      } else if (!dep_reachable) {
        return {false, "cannot list the releases of \"" + dep.name + "\" to pick one in " + dep.range};
      } else if (version == "dev") {
        pick = "dev";
        if (log) log("warn", "\"" + dep.name + "\" has no release in " + dep.range + "; the dev install takes its main");
      } else {
        return {false, label(e->name, version) + " needs \"" + dep.spec() + "\", which has no " +
                           (prerelease(version) ? "release or prerelease" : "release") + " in that range" +
                           (prerelease(version) ? "" : " (prereleases count only for a prerelease app)")};
      }
    }
    if (auto r = install(dep.name, pick, repo_root, log, force, e->name); !r.ok) return r;
    built_with[dep.name] = installed_version(dep.name);
  }

  // 3. clone and build it; candidates in order, "" is the remote's default branch
  const std::string url = repo_url(*e);
  const std::vector<std::string> refs = version == "dev" ? std::vector<std::string>{""}  // the default branch, whatever its name
                                                         : std::vector<std::string>{"v" + version, version};
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
    std::vector<std::string> cmd = {"git", "-c", "advice.detachedHead=false", "clone", "--depth", "1"};
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
    return {false, "cannot clone " + url + " at " + version + (last.empty() ? "" : ": " + last) +
                       " (if the repository is private, give git access to it, e.g. `gh auth setup-git`, or set " +
                       repo_env(e->name) + " to a copy you can read)"};
  }
  std::string commit;
  run_command({"git", "-C", dir.string(), "rev-parse", "HEAD"}, {}, {}, nullptr, &commit);

  if (auto built = run_build(*e, dir, log); !built.ok) {
    restore();
    return built;
  }

  fs::remove_all(prev, ec);
  std::ofstream(dir / kMarker) << json{{"version", version}, {"ref", ref}, {"commit", trimmed(commit)},
                                       {"output", e->output}, {"deps", built_with}}.dump(2) << '\n';
  std::string msg = "installed \"" + e->name + "\" " + version + " in " + dir.string();
  const char* sep = ", with ";
  for (const auto& [dep, v] : built_with.items()) msg += std::exchange(sep, ", ") + dep + " " + v.get<std::string>();
  if (log) log("info", msg);
  return {true, msg};
}

}  // namespace

ToolkitResult install_app(std::string_view name, std::string_view requested, const fs::path& repo_root,
                          const ToolkitLog& log, bool force) {
  return install(name, requested, repo_root, log, force, {});
}

ToolkitResult uninstall_app(std::string_view name, const fs::path& repo_root, const ToolkitLog& log) {
  std::string error;
  const auto list = load(repo_root, error);
  if (!error.empty()) return {false, error};
  if (!find(list, name)) return {false, "unknown app \"" + std::string(name) + "\""};
  if (!installed(name)) return {false, "\"" + std::string(name) + "\" is not installed"};
  for (const auto& other : list)
    if (installed(other.name) && std::ranges::find(other.deps, name, &AppDep::name) != other.deps.end())
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
    const auto newest = newest_fitting(remote_versions(*e), dependents(list, name, {}));
    const auto have = parse_version(version), best = newest ? parse_version(*newest) : std::nullopt;
    return have && best && *best > *have;
  }
  std::string out;  // a dev install tracks the default branch: outdated when it moved on
  if (run_command({"git", "ls-remote", repo_url(*e), "HEAD"}, {}, {}, nullptr, &out) != 0) return false;
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
