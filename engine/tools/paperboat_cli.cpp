// The Paperboat unified CLI: build and start backends, list running backends,
// manage the web application, orchestrate examples, and install/browse toolkits.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/platform.hpp"
#include "paperboat/toolkit/update.hpp"
#include "paperboat/toolkit/installer.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#include "paperboat_tui.hpp"

namespace {

using nlohmann::json;
namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
namespace tui = paperboat::tui;
namespace platform = paperboat::platform;

using Command = int (*)(const std::vector<std::string_view>&);
namespace fs = std::filesystem;

const tk::Registry& full_registry() {
  static const tk::Registry r = [] {
    tk::Registry all = tk::builtins();
#ifdef PAPERBOAT_HAS_OPENCV
    tk::opencv::register_opencv(all);
#endif
    return all;
  }();
  return r;
}

bool launch_console(const std::vector<std::string>& args);

void print_help() {
  std::cout << R"(paperboat - Unified Paperboat command-line tool

Usage:
  paperboat <command> [arguments...]

Start and stop (the web app, backends and programs):
  start [target] [options]   Without a target: the web app (dev server, port 3047).
                             A backend (folder, name or .ppk): build it and run it in the foreground.
                             A program from apps.toml (e.g. squero-studio-om): run it.
                               --port <N>      Listen port (backend 8765, web app 3047, squero-studio-om 59201)
                               --daemon        Run in the background (the web app and programs)
                               --host <H>      The web app's listen host (default 0.0.0.0)
                             Backend only:
                               --no-build      Skip the C++ build step
                               --clean         Clean the build directory first
                               --time          Print build and startup timing
                               --verbose       Verbose build output
                               --var <key=val> Set a default variable value
  stop [target | --all]      Without a target: the web app. Otherwise a backend (port, name or folder) or a
                             program. --all stops every running backend.
  restart [target]           Restart the web app, or a backend or program
  list, ps [--json]          Everything running, with a KIND column: backend or app
  run [path | pkg.ppk]       Start a backend (the same options as start)

Backends:
  backends [--json]          List the backends you can run (examples, installed)
  build [path] [--clean]     Compile a C++ backend
  rebuild [path]             Clean and compile a C++ backend (build --clean)
  open <file.ppj> [--run] [--port <N>] [--no-build]
                             Open a saved project file with its backend (--run: headless)

Web app:
  app start|stop|restart     The same as start, stop and restart without a target
  app build                  Run the production frontend build
  (start --daemon and app start --daemon keep it in the background; stop ends it)

Examples Orchestration:
  examples start [--dir <P>] Launch all example backends (ports 8765-8773)
  examples stop              Stop all example backends
  examples status            Show status of example backends

Toolkits:
  toolkit list [name] [--json]
                             List all toolkits, or inspect blocks/instruments of <name>
  toolkit install <name>     Install an optional toolkit (e.g. opencv)
  toolkit update <name|--all>
                             Update an installed toolkit, or every installed one
  toolkit uninstall <name>   Remove an optional toolkit
  toolkit versions <name>    Query remote versions/tags for a toolkit

Verification & Packaging:
  check [path] [--fast] [--json | --json-stream]
                             4-stage verification (manifest, catalog, smoke, golden)
  pack [path] [--out <pkg>] [--json-stream]
                             Package backend into .ppk archive
  unpack <pkg> [--dir <dir>] [--json-stream]
                             Unpack and verify .ppk archive
                             --json-stream prints one JSON event per line as work goes

Updating:
  update [--check] [--no-build]
                             Pull this repository (fast-forward only), rebuild the engine and console
                             when it changed, pull the sibling checkouts of the apps in apps.toml and
                             rebuild them, and refresh installed apps. --check only reports.

Packages (apps from other repositories, see apps.toml):
  pkg list [--json]          The programs and libraries, installed or not (KIND column)
  pkg install <name>[@<V>]   Clone a release (default: newest, else main) and build it
  pkg update <name|--all>    Install the newest release of an installed app
  pkg uninstall <name>       Remove an app
  pkg versions <name>        Release versions on GitHub, newest first
  pkg path <name>            The installed app's output folder

Console:
  (no command)               Open the interactive console (apps/cli) on a terminal;
                             the basic prompt when it isn't built or PAPERBOAT_NO_CONSOLE is set
  repl                       The basic prompt
  top                        The console's process dashboard
  commands [--json]          The command table (for completion)

General:
  version, --version         Show CLI version
  help, --help               Show this help message
)";
}

int cmd_start(const std::vector<std::string_view>& args);
std::optional<tk::AppEntry> find_program(std::string_view target);

int cmd_run(const std::vector<std::string_view>& args) {
  core::BackendRunOptions opts;
  std::string path;

  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--port" && i + 1 < args.size()) {
      opts.port = std::stoi(std::string(args[++i]));
    } else if (a == "--no-build") {
      opts.build = false;
    } else if (a == "--clean") {
      opts.clean = true;
    } else if (a == "--time") {
      opts.time_metrics = true;
    } else if (a == "--verbose") {
      opts.verbose = true;
    } else if (a == "--var" && i + 1 < args.size()) {
      std::string setting(args[++i]);
      auto eq = setting.find('=');
      if (eq != std::string::npos && eq > 0) {
        opts.vars.emplace_back(setting.substr(0, eq), setting.substr(eq + 1));
      }
    } else if (!a.starts_with("-") && path.empty()) {
      path = std::string(a);
    }
  }

  if (find_program(path)) return cmd_start(args);  // run squero-studio-om: a program, started as `start` does
  if (path.empty() && core::BackendManager::detect_backend_kind(fs::current_path()) == core::BackendKind::Unknown) {
    launch_console({"run"});  // the backend picker; returns only when the console can't open
  }
  return core::BackendManager::run_backend(path, opts);
}

int cmd_open(const std::vector<std::string_view>& args) {
  std::string file_path;
  core::BackendRunOptions opts;
  bool run_headless = false;
  int port = 0;

  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--run") {
      run_headless = true;
    } else if (a == "--port" && i + 1 < args.size()) {
      port = std::stoi(std::string(args[++i]));
    } else if (a == "--no-build") {
      opts.build = false;
    } else if (!a.starts_with("-") && file_path.empty()) {
      file_path = std::string(a);
    }
  }

  if (file_path.empty()) {
    std::cerr << "usage: paperboat open <file.ppj|file.paperboat.json> [--run] [--port <port>] [--no-build]\n";
    return 2;
  }

  const fs::path p(file_path);
  if (!fs::is_regular_file(p)) {
    std::cerr << "error: file not found: " << file_path << '\n';
    return 1;
  }

  json proj;
  try {
    std::ifstream in(p);
    in >> proj;
  } catch (const std::exception& err) {
    std::cerr << "error: invalid project file: " << err.what() << '\n';
    return 1;
  }

  if (!proj.is_object() || !proj.contains("paperboat") || !proj["paperboat"].is_number_integer()) {
    std::cerr << "error: invalid project file: missing \"paperboat\" format version\n";
    return 1;
  }
  if (proj["paperboat"] != 0) {
    std::cerr << "error: unsupported project version " << proj["paperboat"] << " (this tool supports version 0)\n";
    return 1;
  }
  if (!proj.contains("backend") || !proj["backend"].is_string()) {
    std::cerr << "error: invalid project file: missing \"backend\" specification\n";
    return 1;
  }
  if (!proj.contains("flow") || !proj["flow"].is_object()) {
    std::cerr << "error: invalid project file: missing \"flow\" graph\n";
    return 1;
  }

  const std::string backend_name = proj["backend"].get<std::string>();
  const fs::path backend_dir = core::BackendManager::resolve_backend_dir(backend_name);
  if (backend_dir.empty() || !fs::is_directory(backend_dir)) {
    std::cerr << "error: backend \"" << backend_name << "\" not found\n";
    return 1;
  }

  // Check required toolkits
  const auto manifest = core::BackendManager::load_manifest(backend_dir);
  const auto installed_toolkits = tk::list_toolkits();
  for (const auto& req : manifest.toolkits) {
    bool found = false;
    for (const auto& t : installed_toolkits) {
      if (t.name == req) {
        if (t.installed || t.builtin) found = true;
        break;
      }
    }
    if (!found) {
      std::cerr << "error: required toolkit \"" << req << "\" is not installed\n";
      return 1;
    }
  }

  opts.port = port;
  opts.project_file = fs::canonical(p).string();
  opts.run = run_headless;

  if (run_headless) {
    return core::BackendManager::run_backend(backend_dir, opts);
  }

  core::BackendManager::start_app();
  std::cout << "[ok] opening project \"" << p.filename().string() << "\" with backend " << backend_name << '\n';
  return core::BackendManager::run_backend(backend_dir, opts);
}

int cmd_build(const std::vector<std::string_view>& args) {
  std::string path;
  bool clean = false;
  for (auto a : args) {
    if (a == "--clean") clean = true;
    else if (!a.starts_with("-") && path.empty()) path = std::string(a);
  }
  const fs::path dir = core::BackendManager::resolve_backend_dir(path);
  if (core::BackendManager::detect_backend_kind(dir) != core::BackendKind::Cpp) {
    std::cerr << "error: not a C++ backend: " << (path.empty() ? dir.string() : path) << '\n';
    return 2;
  }
  std::string err;
  double elapsed = 0.0;
  if (!core::BackendManager::ensure_built(dir, clean, /*verbose=*/true, &err, &elapsed)) {
    std::cerr << "error: build failed: " << err << '\n';
    return 1;
  }
  std::cout << "[ok] built " << dir.filename().string() << " in " << std::fixed << std::setprecision(1) << elapsed
            << " s\n";
  return 0;
}

int cmd_rebuild(const std::vector<std::string_view>& args) {
  auto with_clean = args;
  with_clean.emplace_back("--clean");
  return cmd_build(with_clean);
}

// The first sentence of a descriptor's opening comment ("# Pole on a cart, balanced by PID
// blocks. Served by ..."): the one-line description the picker shows.
std::string describe(const fs::path& dir) {
  std::ifstream in(dir / "descriptor.toml");
  std::string line;
  if (!std::getline(in, line) || !line.starts_with("# ")) return {};
  line = line.substr(2);
  if (auto dot = line.find(". "); dot != std::string::npos) line = line.substr(0, dot);
  if (!line.empty() && line.back() == '.') line.pop_back();
  return line;
}

int cmd_backends(const std::vector<std::string_view>& args) {
  bool json_out = false;
  for (auto a : args) {
    if (a == "--json") json_out = true;
  }

  // Examples first (in their port order), then the user's installed backends; by name.
  std::map<std::string, json> found;
  std::vector<std::string> order;
  const auto add = [&](const fs::path& dir, std::string source, int fallback_port) {
    const auto kind = core::BackendManager::detect_backend_kind(dir);
    if (kind == core::BackendKind::Unknown) return;
    const auto manifest = core::BackendManager::load_manifest(dir);
    std::string name = manifest.has_manifest && !manifest.name.empty() ? manifest.name : dir.filename().string();
    for (std::string_view suffix : {"-cpp", "-python"}) {
      if (name.ends_with(suffix)) name.resize(name.size() - suffix.size());
    }
    if (found.contains(name)) return;
    order.push_back(name);
    found[name] = {
        {"name", name},
        {"language", kind == core::BackendKind::Cpp ? "cpp" : "python"},
        {"port", manifest.has_manifest && manifest.port > 0 ? manifest.port : fallback_port},
        {"dir", dir.string()},
        {"description", describe(dir)},
        {"source", std::move(source)},
    };
  };
  for (const auto& ex : core::BackendManager::get_examples()) add(ex.dir, "example", ex.port);
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(core::BackendManager::user_backends_dir(), ec)) {
    if (entry.is_directory()) add(entry.path(), "installed", 8765);
  }

  if (json_out) {
    json arr = json::array();
    for (const auto& name : order) arr.push_back(found[name]);
    std::cout << arr.dump(2) << '\n';
    return 0;
  }
  if (order.empty()) {
    std::cout << "No backends found. Set PAPERBOAT_EXAMPLES_DIR or unpack a .ppk.\n";
    return 0;
  }
  std::cout << std::left << std::setw(20) << "NAME" << std::setw(9) << "TYPE" << std::setw(7) << "PORT"
            << "DESCRIPTION\n";
  for (const auto& name : order) {
    const auto& b = found[name];
    std::cout << std::left << std::setw(20) << name << std::setw(9) << b["language"].get<std::string>()
              << std::setw(7) << b["port"].get<int>() << b["description"].get<std::string>() << '\n';
  }
  return 0;
}

// The first sentence of an app's description, for the one-line listings.
std::string first_sentence(std::string text) {
  if (const auto dot = text.find(". "); dot != std::string::npos) text.resize(dot);
  if (!text.empty() && text.back() == '.') text.pop_back();
  return text;
}

constexpr int kWebAppPort = 3047;

// An app from apps.toml (squero-studio-om): `target` is the name, or the command that launches it (`squero`).
// A path or a ./relative name is never one: that is a backend folder.
std::optional<tk::AppEntry> find_program(std::string_view target) {
  if (target.empty() || target.starts_with('.') || target.find_first_of("/\\") != std::string_view::npos) return std::nullopt;
  for (auto& app : tk::list_apps())
    if (app.name == target || (!app.launch.empty() && app.launch == target)) return app;
  return std::nullopt;
}

// What is running, backends and apps alike; `kind` says which.
struct RunningItem {
  std::string kind;  // "backend" | "app"
  std::string name;
  std::string detail;
  int port = 0;
  int pid = 0;
  std::string url;
  json extra = json::object();
};

std::vector<RunningItem> running_items() {
  std::vector<RunningItem> items;
  for (const auto& b : core::BackendManager::list_running_backends()) {
    items.push_back({"backend", b.domain.empty() ? "unknown" : b.domain, b.engine, b.port, b.pid, b.ws_url,
                     {{"engine", b.engine}, {"domain", b.domain}, {"ws_url", b.ws_url}, {"cwd", b.cwd.string()},
                      {"backend_dir", b.backend_dir.string()}, {"started_at", b.started_at}}});
  }
  const auto app = [&](const std::string& name, const std::string& detail, int port) {
    if (!core::BackendManager::is_port_listening(port)) return;
    items.push_back({"app", name, detail, port, core::BackendManager::find_pid_by_port(port),
                     "http://localhost:" + std::to_string(port), {{"engine", ""}, {"domain", name}, {"ws_url", ""}}});
  };
  app("paperboat", "the web app (dev server)", kWebAppPort);
  for (const auto& a : tk::list_apps())
    if (a.kind == "program" && a.port > 0) app(a.name, first_sentence(a.description), a.port);
  return items;
}

int cmd_list(const std::vector<std::string_view>& args) {
  bool json_out = false;
  for (auto a : args) {
    if (a == "--json") json_out = true;
  }

  const auto items = running_items();
  if (json_out) {
    json arr = json::array();
    for (const auto& it : items) {
      json row = {{"kind", it.kind}, {"name", it.name}, {"port", it.port}, {"pid", it.pid}, {"url", it.url}};
      row.update(it.extra);
      arr.push_back(std::move(row));
    }
    std::cout << arr.dump(2) << '\n';
    return 0;
  }

  if (items.empty()) {
    std::cout << "Nothing is running. `paperboat start` starts the web app; `paperboat start <backend|program>` the rest.\n";
    return 0;
  }

  std::cout << std::left << std::setw(9) << "KIND" << std::setw(7) << "PORT" << std::setw(8) << "PID" << std::setw(22)
            << "NAME" << std::setw(28) << "DETAIL" << "URL\n";
  for (const auto& it : items) {
    std::cout << std::left << std::setw(9) << it.kind << std::setw(7) << it.port << std::setw(8) << it.pid
              << std::setw(22) << it.name << std::setw(28) << it.detail << it.url << '\n';
  }
  return 0;
}

int cmd_app(const std::vector<std::string_view>& args);
Command find_command(std::string_view name);

// `start`, `stop` and `restart` with no target are for the main app (the web app); `app <verb>` is the same.
int main_app(std::string_view verb, const std::vector<std::string_view>& args) {
  std::vector<std::string_view> forwarded = {verb};
  forwarded.insert(forwarded.end(), args.begin(), args.end());
  return cmd_app(forwarded);
}

// The first argument that is not a flag or a flag's value: what `start`/`stop`/`restart` act on.
std::string target_of(const std::vector<std::string_view>& args) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--port" || args[i] == "--host" || args[i] == "--var" || args[i] == "--version") ++i;
    else if (!args[i].starts_with("-")) return std::string(args[i]);
  }
  return {};
}

std::vector<std::string_view> without(const std::vector<std::string_view>& args, std::string_view target) {
  std::vector<std::string_view> rest;
  bool dropped = false;
  for (auto a : args) {
    if (!dropped && a == target) dropped = true;
    else rest.push_back(a);
  }
  return rest;
}

// --port N, else the program's own port from apps.toml.
int port_of(const tk::AppEntry& app, const std::vector<std::string_view>& args) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == "--port") return std::stoi(std::string(args[i + 1]));
  return app.port;
}

int stop_program(tk::AppEntry app, const std::vector<std::string_view>& args = {}) {
  if (app.kind == "library") {
    std::cerr << "error: " << app.name << " is a library, not a program\n";
    return 1;
  }
  app.port = port_of(app, args);
  if (app.port <= 0) {
    std::cerr << "error: " << app.name << " has no port in apps.toml, so paperboat can't find it to stop it\n";
    return 1;
  }
  if (!core::BackendManager::is_port_listening(app.port)) {
    std::cout << app.name << " is not running\n";
    return 0;
  }
  const int pid = core::BackendManager::find_pid_by_port(app.port);
  core::BackendManager::free_port(app.port, 2000);
  if (core::BackendManager::is_port_listening(app.port)) {
    std::cerr << "error: could not stop " << app.name << " (pid " << pid << ", port " << app.port << ")\n";
    return 1;
  }
  std::cout << "stopped " << app.name << " (pid " << pid << ", port " << app.port << ")\n";
  return 0;
}

fs::path find_in_path(std::string_view name) {
  if (name.empty()) return {};
  const char* path_env = std::getenv("PATH");
  if (!path_env) return {};
  std::stringstream ss(path_env);
  std::string dir;
  std::error_code ec;
#ifdef _WIN32
  constexpr char delim = ';';
#else
  constexpr char delim = ':';
#endif
  while (std::getline(ss, dir, delim)) {
    if (dir.empty()) continue;
    const fs::path p = fs::path(dir) / name;
    if (fs::is_regular_file(p, ec)) return p;
#ifdef _WIN32
    const fs::path p_exe = fs::path(dir) / (std::string(name) + ".exe");
    if (fs::is_regular_file(p_exe, ec)) return p_exe;
#endif
  }
  return {};
}

fs::path resolve_app_binary(const tk::AppEntry& app) {
  const fs::path root = tk::find_repo_root();
  std::error_code ec;
  // 1. Sibling checkout
  if (!root.empty()) {
    const fs::path sibling = root.parent_path() / app.name;
    const fs::path cand1 = sibling / app.launch;
    if (fs::is_regular_file(cand1, ec)) return cand1;
    const fs::path cand2 = sibling / "server" / "build" / fs::path(app.launch).filename();
    if (fs::is_regular_file(cand2, ec)) return cand2;
  }
  // 2. Installed app directory
  const fs::path cand3 = tk::apps_dir() / app.name / app.launch;
  if (fs::is_regular_file(cand3, ec)) return cand3;
  const fs::path cand4 = tk::apps_dir() / app.name / "server" / "build" / fs::path(app.launch).filename();
  if (fs::is_regular_file(cand4, ec)) return cand4;
  // 3. System PATH
  fs::path in_path = find_in_path(app.launch);
  if (!in_path.empty()) return in_path;
  in_path = find_in_path(fs::path(app.launch).filename().string());
  if (!in_path.empty()) return in_path;

  return {};
}

// Run a program in the foreground through its launch command, or --daemon: the same, detached, with a log.
int start_program(const tk::AppEntry& app, std::vector<std::string_view> rest) {
  if (app.launch.empty()) {
    std::cerr << "error: " << app.name << " has no launch command in apps.toml\n";
    return 1;
  }
  const auto daemon = std::ranges::find(rest, std::string_view("--daemon"));
  const bool is_daemon = (daemon != rest.end());
  if (const Command launch = find_command(app.launch)) {
    if (!is_daemon) return launch(rest);
    rest.erase(daemon);
    std::vector<std::string> argv = {platform::exe_path().string(), "start", app.name};
    for (auto a : rest) argv.emplace_back(a);
    const fs::path log = platform::shared_tmp_dir() / ("paperboat-" + app.name + ".log");
    const int pid = core::ProcessRunner::run_detached(argv, {}, {}, log);
    if (pid <= 0) {
      std::cerr << "error: could not start " << app.name << '\n';
      return 1;
    }
    std::cout << "started " << app.name << " (pid " << pid << ", port " << port_of(app, rest) << ") · log " << log.string() << '\n';
    return 0;
  }

  const fs::path bin = resolve_app_binary(app);
  if (bin.empty()) {
    std::cerr << "error: cannot find " << app.launch << " for " << app.name
              << " (run `paperboat pkg install " << app.name << "`)\n";
    return 1;
  }

  if (is_daemon) rest.erase(daemon);
  std::vector<std::string> argv = {bin.string()};
  for (auto a : rest) argv.emplace_back(a);

  if (is_daemon) {
    const fs::path log = platform::shared_tmp_dir() / ("paperboat-" + app.name + ".log");
    const int pid = core::ProcessRunner::run_detached(argv, {}, {}, log);
    if (pid <= 0) {
      std::cerr << "error: could not start " << app.name << '\n';
      return 1;
    }
    std::cout << "started " << app.name << " (pid " << pid << ", port " << port_of(app, rest) << ") · log " << log.string() << '\n';
    return 0;
  }

  return core::ProcessRunner::run_stream(argv, {}, {}, [](std::string_view stream, std::string_view line) {
    if (stream == "stderr") std::cerr << line << '\n';
    else std::cout << line << '\n';
  });
}

int cmd_restart(const std::vector<std::string_view>& args) {
  const std::string target = target_of(args);
  if (target.empty()) return main_app("restart", args);
  if (const auto app = find_program(target)) {
    if (const int rc = stop_program(*app, args); rc != 0) return rc;
    auto rest = without(args, target);
    rest.emplace_back("--daemon");
    return start_program(*app, rest);
  }
  core::BackendRunOptions opts;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--port" && i + 1 < args.size()) {
      opts.port = std::stoi(std::string(args[++i]));
    }
  }
  return core::BackendManager::restart_backend(target, opts) ? 0 : 1;
}

int cmd_stop(const std::vector<std::string_view>& args) {
  const bool all = std::ranges::find(args, std::string_view("--all")) != args.end();
  const std::string target = target_of(args);
  if (!all && target.empty()) return main_app("stop", args);
  if (const auto app = find_program(target)) return stop_program(*app, args);
  return core::BackendManager::stop_backend(target, all) ? 0 : 1;
}

// start: no target is the web app; a program from apps.toml (start squero-studio-om) runs that; anything else is a backend.
int cmd_start(const std::vector<std::string_view>& args) {
  const std::string target = target_of(args);
  if (target.empty()) return main_app("start", args);
  if (const auto app = find_program(target)) {
    if (app->kind == "library") {
      std::cerr << "error: " << app->name << " is a library, not a program\n";
      return 1;
    }
    return start_program(*app, without(args, target));
  }
  std::error_code ec;
  if (!fs::exists(target, ec) && !fs::is_directory(core::BackendManager::resolve_backend_dir(target), ec)) {
    std::cerr << "error: \"" << target << "\" is neither a backend nor a program\n"
              << "  backends: `paperboat backends` (or a folder or .ppk)   programs: `paperboat pkg list`\n";
    return 1;
  }
  return cmd_run(args);
}

int cmd_app(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "usage: paperboat app <start|stop|restart|build> [options]\n";
    return 2;
  }
  auto sub = args[0];
  int port = 3047;
  std::string host = "0.0.0.0";
  bool daemon = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--port" && i + 1 < args.size()) {
      port = std::stoi(std::string(args[++i]));
    } else if (args[i] == "--host" && i + 1 < args.size()) {
      host = std::string(args[++i]);
    } else if (args[i] == "--daemon") {
      daemon = true;
    }
  }

  if (sub == "start") {
    return core::BackendManager::start_app(port, host, daemon) ? 0 : 1;
  } else if (sub == "stop") {
    return core::BackendManager::stop_app(port) ? 0 : 1;
  } else if (sub == "restart") {
    return core::BackendManager::restart_app(port, host) ? 0 : 1;
  } else if (sub == "build") {
    return core::BackendManager::build_app() ? 0 : 1;
  } else {
    std::cerr << "error: unknown app subcommand: " << sub << '\n';
    return 2;
  }
}

int cmd_examples(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "usage: paperboat examples <start|stop|status|fetch> [--dir <path>]\n";
    return 2;
  }
  auto sub = args[0];
  std::string dir;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--dir" && i + 1 < args.size()) {
      dir = std::string(args[++i]);
    }
  }

  if (sub == "fetch") {
    return core::BackendManager::fetch_examples() ? 0 : 1;
  } else if (sub == "start") {
    return core::BackendManager::start_examples(dir) ? 0 : 1;
  } else if (sub == "stop") {
    return core::BackendManager::stop_examples() ? 0 : 1;
  } else if (sub == "status") {
    auto list = core::BackendManager::status_examples(dir);
    std::cout << std::left << std::setw(8) << "PORT"
              << std::setw(26) << "NAME"
              << std::setw(10) << "TYPE"
              << std::setw(12) << "STATUS"
              << std::setw(8) << "PID"
              << "WS URL\n";
    for (const auto& item : list) {
      std::cout << std::left << std::setw(8) << item.port
                << std::setw(26) << item.name
                << std::setw(10) << item.type
                << std::setw(12) << (item.running ? "running" : "idle")
                << std::setw(8) << (item.running ? std::to_string(item.pid) : "-")
                << item.ws_url << '\n';
    }
    return 0;
  } else {
    std::cerr << "error: unknown examples subcommand: " << sub << '\n';
    return 2;
  }
}

int cmd_toolkit(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    launch_console({"toolkit"});  // the toolkit manager; returns only when the console can't open
    std::cerr << "usage: paperboat toolkit <list|install|update|uninstall|versions> [args...]\n";
    return 2;
  }
  auto sub = args[0];
  bool json_out = false;
  bool rebuild = true;
  bool all = false;
  std::string name;
  std::string version;

  for (std::size_t i = 1; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--json") json_out = true;
    else if (a == "--all") all = true;
    else if (a == "--no-build") rebuild = false;
    else if ((a == "--version" || a == "-v") && i + 1 < args.size()) {
      version = std::string(args[++i]);
    }
    else if (!a.starts_with("-") && name.empty()) name = std::string(a);
  }

  if (sub == "list") {
    if (!name.empty()) {
      auto res = tk::inspect_toolkit(name, full_registry());
      if (!res.found) {
        std::cerr << "paperboat toolkit: unknown toolkit or package \"" << name << "\"\n";
        return 1;
      }
      if (json_out) {
        std::cout << tk::toolkit_inspect_json(res) << '\n';
      } else {
        std::cout << tk::toolkit_inspect_text(res);
      }
      return 0;
    }
    auto list = tk::list_toolkits();
    if (json_out) {
      std::cout << tk::toolkits_json(list) << '\n';
      return 0;
    }
    for (const auto& t : list) {
      const char* status = t.builtin ? "built-in toolkit" : (t.installed ? "installed" : "not installed");
      std::cout << t.name << "\t" << status << "\t" << (t.repo.empty() ? "-" : t.repo);
      if (!t.description.empty()) std::cout << "\t" << t.description;
      std::cout << '\n';
    }
    return 0;
  }

  if (sub == "versions") {
    if (name.empty()) {
      std::cerr << "error: toolkit name required (e.g. `paperboat toolkit versions opencv`)\n";
      return 2;
    }
    auto vers = tk::list_toolkit_versions(name);
    if (vers.empty()) {
      std::cout << "No remote version tags found for toolkit \"" << name << "\".\n";
      return 0;
    }
    for (const auto& v : vers) {
      std::cout << v << '\n';
    }
    return 0;
  }

  if (sub == "update" && all) {
    int failed = 0;
    for (const auto& t : tk::list_toolkits()) {
      if (!t.installed || t.builtin) continue;
      std::vector<std::string_view> one = {"update", t.name};
      if (!rebuild) one.emplace_back("--no-build");
      failed += cmd_toolkit(one) != 0;
    }
    return failed ? 1 : 0;
  }

  if (name.empty()) {
    std::cerr << "error: toolkit name required\n";
    return 2;
  }

  auto log = [](std::string_view level, std::string_view text) {
    (level == "error" ? std::cerr : std::cout) << "[" << level << "] " << text << '\n';
  };

  tk::HostBuild host{.source_dir = {}, .build_dir = {}, .skip_rebuild = !rebuild};
  tk::ToolkitResult res;
  if (sub == "install" || sub == "add" || sub == "update") {
    res = tk::install_toolkit(name, version, {}, host, log);
  } else if (sub == "uninstall" || sub == "remove") {
    res = tk::uninstall_toolkit(name, {}, host, log);
  } else {
    std::cerr << "error: unknown toolkit subcommand: " << sub << '\n';
    return 2;
  }

  if (!res.ok) {
    std::cerr << "paperboat toolkit: " << res.message << '\n';
    return 1;
  }
  return 0;
}

// paperboat pkg: the package manager for apps in other repositories (apps.toml). Same shape as
// `toolkit`, but into a per-user store, and what's built is an app, not a toolkit of this engine.
int cmd_pkg(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "usage: paperboat pkg <list|install|update|uninstall|versions|path> [name[@version]] [--version V] [--all] [--json]\n";
    return 2;
  }
  const auto sub = args[0];
  bool json_out = false;
  bool all = false;
  std::string name;
  std::string version;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--json") json_out = true;
    else if (args[i] == "--all") all = true;
    else if ((args[i] == "--version" || args[i] == "-v") && i + 1 < args.size()) version = std::string(args[++i]);
    else if (!args[i].starts_with("-") && name.empty()) name = std::string(args[i]);
  }
  if (const auto at = name.find('@'); at != std::string::npos) {  // name@version
    version = name.substr(at + 1);
    name.resize(at);
  }

  if (sub == "list") {
    const auto list = tk::list_apps();
    if (json_out) {
      json arr = json::array();
      for (const auto& a : list)
        arr.push_back({{"name", a.name}, {"repo", a.repo}, {"description", a.description},
                       {"installed", a.installed}, {"version", a.version}, {"deps", a.deps}, {"kind", a.kind},
                       {"launch", a.launch}, {"port", a.port}});
      std::cout << arr.dump(2) << '\n';
      return 0;
    }
    if (list.empty()) std::cerr << "no apps (apps.toml not found in the repository root)\n";
    std::cout << std::left << std::setw(20) << "NAME" << std::setw(9) << "KIND" << std::setw(15) << "VERSION" << "DESCRIPTION\n";
    for (const auto& a : list)
      std::cout << std::left << std::setw(20) << a.name << std::setw(9) << a.kind << std::setw(15)
                << (a.installed ? a.version : "not installed") << a.description << '\n';
    return 0;
  }
  if (sub == "update" && all) {
    int failed = 0;
    for (const auto& a : tk::list_apps()) {
      if (!a.installed) continue;
      const auto r = tk::install_app(a.name, {}, {}, [](std::string_view l, std::string_view t) {
        (l == "error" ? std::cerr : std::cout) << "[" << l << "] " << t << '\n';
      });
      if (!r.ok) std::cerr << "paperboat pkg: " << r.message << '\n';
      failed += !r.ok;
    }
    return failed ? 1 : 0;
  }
  if (name.empty()) {
    std::cerr << "error: app name required (e.g. `paperboat pkg " << sub << " squero-studio-om`)\n";
    return 2;
  }
  if (sub == "versions") {
    const auto versions = tk::list_app_versions(name);
    for (const auto& v : versions) std::cout << v << '\n';
    if (versions.empty()) std::cerr << "no release tags for \"" << name << "\"; install takes main (\"dev\")\n";
    return 0;
  }
  if (sub == "path") {
    const auto out = tk::app_output(name);
    if (out.empty()) {
      std::cerr << "paperboat pkg: \"" << name << "\" is not installed (paperboat pkg install " << name << ")\n";
      return 1;
    }
    std::cout << out.string() << '\n';
    return 0;
  }
  const auto log = [](std::string_view level, std::string_view text) {
    (level == "error" ? std::cerr : std::cout) << "[" << level << "] " << text << '\n';
  };
  tk::ToolkitResult res;
  if (sub == "install" || sub == "add" || sub == "update") res = tk::install_app(name, version, {}, log);
  else if (sub == "uninstall" || sub == "remove") res = tk::uninstall_app(name, {}, log);
  else {
    std::cerr << "error: unknown pkg subcommand: " << sub << '\n';
    return 2;
  }
  if (!res.ok) std::cerr << "paperboat pkg: " << res.message << '\n';
  return res.ok ? 0 : 1;
}

// The build directory of the running binary (engine/build/dev, engine/build, ...).
fs::path running_build_dir(const fs::path& root) {
  fs::path p = platform::exe_path();
  for (p = p.parent_path(); !p.empty() && p != p.root_path() && p.has_parent_path(); p = p.parent_path())
    if (fs::is_regular_file(p / "CMakeCache.txt")) return p;
  return root / "engine" / "build";
}

// paperboat update [--check] [--no-build]: pull this repository (fast-forward only), the sibling
// checkouts of the apps in apps.toml, and rebuild what changed; then refresh the apps `pkg` installed.
int cmd_update(const std::vector<std::string_view>& args) {
  bool check_only = false;
  bool build = true;
  for (const auto a : args) {
    if (a == "--check") check_only = true;
    else if (a == "--no-build") build = false;
    else {
      std::cerr << "usage: paperboat update [--check] [--no-build]\n";
      return 2;
    }
  }
  const fs::path root = tk::find_repo_root();
  if (root.empty()) {
    std::cerr << "paperboat update: needs a git checkout of paperboat (no toolkits.toml found from here)\n";
    return 1;
  }
  const auto log = [](std::string_view level, std::string_view text) {
    (level == "error" ? std::cerr : std::cout) << "  " << text << '\n';
  };
  const auto say = [](const std::string& name, const std::string& text) {
    std::cout << std::left << std::setw(20) << name << text << '\n';
  };
  int failed = 0;
  bool pulled = false;

  // Fetch one checkout and fast-forward it; `changed` gets the paths the pull touched.
  const auto sync = [&](const std::string& name, const fs::path& dir, std::vector<std::string>& changed) {
    const auto st = tk::check_checkout(dir);
    if (!st.error.empty()) {
      say(name, "skipped: " + st.error);
      ++failed;
    } else if (st.behind == 0) {
      say(name, "up to date (" + st.upstream + ")");
    } else {
      say(name, std::to_string(st.behind) + " commit(s) behind " + st.upstream);
      for (const auto& subject : st.incoming) std::cout << "    " << subject << '\n';
      if (check_only) return;
      if (const auto r = tk::pull_checkout(dir, &changed); !r.ok) {
        say(name, "not updated: " + r.message);
        ++failed;
      } else {
        pulled = true;
      }
    }
  };
  // A build step with its output kept quiet; the tail of it is shown when it fails.
  const auto step = [&](const std::string& label, const std::vector<std::string>& cmd, const fs::path& cwd) {
    std::cout << "  $ " << label << '\n';
    std::string out;
    if (tk::run_command(cmd, cwd, {}, nullptr, &out) == 0) return;
    ++failed;
    std::size_t from = out.size();
    for (int lines = 0; from > 0 && lines < 12; ++lines) from = out.rfind('\n', from - 1) == std::string::npos ? 0 : out.rfind('\n', from - 1);
    std::cerr << "  \"" << label << "\" failed:\n" << out.substr(from) << '\n';
  };
  const auto touched = [](const std::vector<std::string>& paths, std::string_view prefix, std::string_view suffix = {}) {
    return std::ranges::any_of(paths, [&](const std::string& p) {
      return p.starts_with(prefix) && (suffix.empty() || p.ends_with(suffix));
    });
  };

  // 1. this repository, then what it builds on
  std::vector<std::string> changed;
  const bool checkout = fs::exists(root / ".git");
  if (checkout) {
    sync("paperboat", root, changed);
  } else {  // an installed package: there is no repository to pull
    const bool npm = platform::exe_path().string().find("node_modules") != std::string::npos;
    if (!npm) {
      say("paperboat", std::string(tui::kVersion) + ", installed from a package: update it with your package manager");
    } else {
      std::string latest;
      if (tk::run_command({"npm", "view", "@paperboatlabs/cli", "version"}, {}, {}, &latest) != 0) {
        say("paperboat", std::string(tui::kVersion) + ", cannot read @paperboatlabs/cli from the npm registry (offline, or not published yet)");
        ++failed;
      } else if (latest == tui::kVersion) {
        say("paperboat", std::string(tui::kVersion) + ", up to date (npm)");
      } else {
        say("paperboat", std::string(tui::kVersion) + ", " + latest + " is available (npm)");
        if (!check_only) step("npm install -g @paperboatlabs/cli@latest", {"npm", "install", "-g", "@paperboatlabs/cli@latest"}, root);
      }
    }
  }
  for (const auto& app : tk::list_apps(root)) {  // after the pull, so a new apps.toml counts
    const fs::path dir = root.parent_path() / app.name;
    if (!checkout || !fs::exists(dir / ".git")) continue;  // not checked out here: `pkg` handles the installed copy
    std::vector<std::string> app_changed;
    sync(app.name, dir, app_changed);
    if (build && !app_changed.empty() && !app.build.empty()) {
      std::cout << "  building " << app.name << '\n';
      if (const auto r = tk::build_app(app.name, dir, root, log); !r.ok) {
        std::cerr << r.message << '\n';
        ++failed;
      }
    }
  }

  // 2. rebuild this repository's own binaries and console
  if (build && !changed.empty()) {
    const bool deps = touched(changed, "package", ".json") || touched(changed, "apps/", "package.json") ||
                      touched(changed, "apps/", "package-lock.json");
    if (deps) step("npm ci", {"npm", "ci"}, root);
    if (deps || touched(changed, "apps/cli/")) step("npm run build:cli", {"npm", "run", "build:cli"}, root);
    step("cmake --build", {"cmake", "--build", running_build_dir(root).string(), "--parallel"}, root);
  }

  // 3. apps installed by `paperboat pkg`
  for (const auto& app : tk::list_apps(root)) {
    if (!app.installed) continue;
    if (!tk::app_outdated(app.name, root)) {
      say(app.name, "installed " + app.version + ", up to date");
      continue;
    }
    say(app.name, "installed " + app.version + ", newer available");
    if (check_only) continue;
    if (const auto r = tk::install_app(app.name, {}, root, log); !r.ok) {
      std::cerr << "paperboat update: " << r.message << '\n';
      ++failed;
    } else {
      pulled = true;
    }
  }

  if (pulled) std::cout << "updated; restart running backends and servers to use it\n";
  return failed ? 1 : 0;
}

json step_json(const core::CheckStep& s) {
  return {{"name", s.name}, {"ok", s.ok}, {"skipped", s.skipped}, {"message", s.message}, {"duration", s.duration_seconds}};
}

// One line of `--json-stream`: an event object, flushed so a reader sees it at once.
void emit(json event) {
  std::cout << event.dump() << std::endl;
}

int cmd_check(const std::vector<std::string_view>& args) {
  std::string path;
  bool fast = false;
  bool json_out = false;
  bool stream = false;

  for (auto a : args) {
    if (a == "--fast") fast = true;
    else if (a == "--json") json_out = true;
    else if (a == "--json-stream") stream = true;
    else if (!a.starts_with("-") && path.empty()) path = std::string(a);
  }

  core::CheckProgress progress;
  if (stream) {
    progress = [](const core::CheckStep& step, bool done) {
      json e = step_json(step);
      e["event"] = done ? "step" : "step-start";
      emit(std::move(e));
    };
  }
  auto res = core::BackendManager::check_backend(path, fast, progress);
  if (json_out || stream) {
    json steps_json = json::array();
    for (const auto& s : res.steps) steps_json.push_back(step_json(s));
    json doc = {
        {"ok", res.ok},
        {"name", res.name},
        {"version", res.version},
        {"language", res.language},
        {"domain", res.domain},
        {"engine", res.engine},
        {"blocks", res.block_count},
        {"objects", res.object_count},
        {"has_starter_graph", res.has_starter_graph},
        {"has_starter_station", res.has_starter_station},
        {"steps", steps_json},
        {"errors", res.errors},
        {"warnings", res.warnings},
        {"missing_toolkits", res.missing_toolkits},
    };
    if (!res.solver.empty()) doc["solver"] = res.solver;
    if (stream) {
      doc["event"] = "result";
      emit(std::move(doc));
    } else {
      std::cout << doc.dump(2) << '\n';
    }
    return res.ok ? 0 : 1;
  }

  std::cout << "backend \"" << (res.name.empty() ? res.domain : res.name)
            << "\" (" << (res.version.empty() ? "0.1.0" : res.version)
            << ", " << (res.language.empty() ? "unknown" : res.language);
  if (!res.solver.empty()) {
    std::cout << ", solver: " << res.solver;
  }
  std::cout << "):\n";

  for (const auto& step : res.steps) {
    if (step.skipped) {
      std::cout << "  [--] " << step.name << ": " << step.message << '\n';
    } else if (step.ok) {
      std::cout << "  [ok] " << step.name << ": " << step.message;
      if (step.duration_seconds > 0.001) {
        std::cout << " (" << std::fixed << std::setprecision(2) << step.duration_seconds << "s)";
      }
      std::cout << '\n';
    } else {
      std::cout << "  [FAIL] " << step.name << ": " << step.message << '\n';
    }
  }

  for (const auto& w : res.warnings) {
    std::cout << "  warning: " << w << '\n';
  }

  if (res.ok) {
    std::cout << "[ok] backend verification passed ("
              << (fast ? "fast check" : "all steps") << ")\n";
    return 0;
  } else {
    std::cerr << "[failed] backend verification failed:\n";
    for (const auto& e : res.errors) {
      std::cerr << "  error: " << e << '\n';
    }
    return 1;
  }
}

core::PackProgress progress_events() {
  return [](int files, int total, uint64_t bytes) {
    emit({{"event", "progress"}, {"files", files}, {"total", total}, {"bytes", bytes}});
  };
}

int cmd_pack(const std::vector<std::string_view>& args) {
  std::string path;
  std::string out_path;

  bool stream = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--out" && i + 1 < args.size()) {
      out_path = std::string(args[++i]);
    } else if (a == "--json-stream") {
      stream = true;
    } else if (!a.starts_with("-") && path.empty()) {
      path = std::string(a);
    }
  }

  if (stream) {
    auto res = core::BackendManager::pack_backend(path, out_path, /*fast=*/true, progress_events());
    emit({{"event", "result"}, {"ok", res.ok}, {"package", res.package_path.string()}, {"name", res.name},
          {"version", res.version}, {"files", res.file_count}, {"bytes", res.total_bytes}, {"sha256", res.sha256},
          {"errors", res.errors}});
    return res.ok ? 0 : 1;
  }

  std::cout << "==> Packing backend " << (path.empty() ? "." : path) << "…\n";
  auto res = core::BackendManager::pack_backend(path, out_path, /*fast=*/true);
  if (!res.ok) {
    std::cerr << "[failed] packing error:\n";
    for (const auto& e : res.errors) {
      std::cerr << "  " << e << '\n';
    }
    return 1;
  }

  double kb = static_cast<double>(res.total_bytes) / 1024.0;
  std::cout << "[ok] package created: " << res.package_path.filename().string()
            << " (" << res.file_count << " files, "
            << std::fixed << std::setprecision(1) << kb << " KB)\n";
  if (!res.sha256.empty()) {
    std::cout << "     sha256: " << res.sha256 << '\n';
  }
  return 0;
}

int cmd_unpack(const std::vector<std::string_view>& args) {
  std::string archive;
  std::string dest_dir;

  bool stream = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--dir" && i + 1 < args.size()) {
      dest_dir = std::string(args[++i]);
    } else if (a == "--json-stream") {
      stream = true;
    } else if (!a.starts_with("-") && archive.empty()) {
      archive = std::string(a);
    }
  }

  if (archive.empty()) {
    std::cerr << "error: package archive path required (e.g. `paperboat unpack my-backend-0.1.0.ppk`)\n";
    return 2;
  }

  if (stream) {
    auto res = core::BackendManager::unpack_backend(archive, dest_dir, /*verify=*/true, progress_events());
    emit({{"event", "result"}, {"ok", res.ok}, {"dir", res.destination_dir.string()}, {"name", res.name},
          {"version", res.version}, {"files", res.file_count}, {"errors", res.errors}});
    return res.ok ? 0 : 1;
  }

  std::cout << "==> Unpacking package " << archive << "…\n";
  auto res = core::BackendManager::unpack_backend(archive, dest_dir, /*verify=*/true);
  if (!res.ok) {
    std::cerr << "[failed] unpack error:\n";
    for (const auto& e : res.errors) {
      std::cerr << "  " << e << '\n';
    }
    return 1;
  }

  std::cout << "[ok] package extracted to " << res.destination_dir.string()
            << " (" << res.file_count << " files verified)\n";
  std::cout << "[ok] backend " << res.name << " v" << res.version << " is ready to run\n";
  return 0;
}

int cmd_commands(const std::vector<std::string_view>& args);
int cmd_top(const std::vector<std::string_view>&);

// What a command's free argument names, for completion: a backend, a toolkit, something
// running (name or port), a .ppk file, or nothing.
struct CommandSpec {
  std::string_view name;
  Command run;
  std::string_view summary;
  std::vector<std::string_view> aliases = {};
  std::vector<std::string_view> subcommands = {};
  std::vector<std::string_view> flags = {};
  std::string_view arg = {};
};

// The subcommands the command line, the basic prompt and the console share (`commands --json`).
const std::vector<CommandSpec>& command_table() {
  static const std::vector<CommandSpec> table = {
      {"start", cmd_start, "Start the web app, or a backend or program (start [backend|program])", {}, {},
       {"--port", "--host", "--daemon", "--no-build", "--clean", "--time", "--verbose", "--var", "--version"}, "target"},
      {"stop", cmd_stop, "Stop the web app, or a backend or program (stop [backend|program])", {}, {}, {"--all", "--port"},
       "target"},
      {"restart", cmd_restart, "Restart the web app, or a backend or program", {}, {}, {"--port"}, "target"},
      {"run", cmd_run, "Build and run a backend (or .ppk) in the foreground", {}, {},
       {"--port", "--no-build", "--clean", "--time", "--verbose", "--var"}, "backend"},
      {"open", cmd_open, "Open a saved project file (.ppj, .paperboat.json)", {}, {},
       {"--run", "--port", "--no-build"}, "project"},
      {"build", cmd_build, "Compile a C++ backend", {}, {}, {"--clean"}, "backend"},
      {"rebuild", cmd_rebuild, "Clean and compile a C++ backend", {}, {}, {}, "backend"},
      {"backends", cmd_backends, "List the backends you can run", {}, {}, {"--json"}},
      {"list", cmd_list, "List what is running, backends and apps (KIND)", {"ps"}, {}, {"--json"}},
      {"app", cmd_app, "The web app: start, stop, restart, build (start/stop/restart do the same)", {},
       {"start", "stop", "restart", "build"}, {"--port", "--host", "--daemon"}},
      {"examples", cmd_examples, "Run all example backends, or fetch them", {}, {"start", "stop", "status", "fetch"}, {"--dir"}},
      {"toolkit", cmd_toolkit, "Manage optional toolkits", {}, {"list", "install", "update", "uninstall", "versions"},
       {"--json", "--all", "--no-build", "--version"}, "toolkit"},
      {"check", cmd_check, "Verify a backend (manifest, catalog, smoke, golden)", {}, {},
       {"--fast", "--json", "--json-stream"}, "backend"},
      {"pack", cmd_pack, "Package a backend as .ppk", {}, {}, {"--out", "--json-stream"}, "backend"},
      {"unpack", cmd_unpack, "Unpack and verify a .ppk", {}, {}, {"--dir", "--json-stream"}, "ppk"},
      {"update", cmd_update, "Pull this repository and its app checkouts, rebuild", {}, {}, {"--check", "--no-build"}},
      {"pkg", cmd_pkg, "Apps from other repositories (package manager)", {},
       {"list", "install", "update", "uninstall", "versions", "path"}, {"--json", "--all", "--version"}},
      {"top", cmd_top, "Process dashboard (console)"},
      {"commands", cmd_commands, "The command table", {}, {}, {"--json"}},
  };
  return table;
}

Command find_command(std::string_view name) {
  for (const auto& c : command_table()) {
    if (c.name == name || std::ranges::find(c.aliases, name) != c.aliases.end()) return c.run;
  }
  return nullptr;
}

int cmd_commands(const std::vector<std::string_view>& args) {
  const bool json_out = std::ranges::find(args, std::string_view("--json")) != args.end();
  json arr = json::array();
  for (const auto& c : command_table()) {
    if (!json_out) {
      std::cout << std::left << std::setw(10) << c.name << c.summary << '\n';
      continue;
    }
    const auto strings = [](const std::vector<std::string_view>& v) {
      return std::vector<std::string>(v.begin(), v.end());
    };
    arr.push_back({{"name", c.name}, {"summary", c.summary}, {"aliases", strings(c.aliases)},
                   {"subcommands", strings(c.subcommands)}, {"flags", strings(c.flags)}, {"arg", c.arg}});
  }
  if (json_out) std::cout << arr.dump(2) << '\n';
  return 0;
}

int cmd_top(const std::vector<std::string_view>&) {
  launch_console({"top"});
  std::cerr << "paperboat top needs the console on a terminal (npm run build:cli); `paperboat list` "
               "shows the same backends\n";
  return 2;
}

// The console (apps/cli, Ink) replaces this process on a terminal. Returns only when it
// can't: not a terminal, PAPERBOAT_NO_CONSOLE set, not built, or no `node` on PATH.
bool launch_console(const std::vector<std::string>& args) {
#ifdef _WIN32
  (void)args;
  return false;  // the console replaces this process with node (execvp); on Windows the basic prompt is used
#else
  if (!platform::is_tty(0) || !platform::is_tty(1) || std::getenv("PAPERBOAT_NO_CONSOLE")) return false;
  std::error_code ec;
  const fs::path exe = platform::exe_path();
  fs::path script;
  if (const char* env = std::getenv("PAPERBOAT_CONSOLE"); env && *env) {
    script = env;
  } else {
    // engine/build/tools/paperboat in a checkout, else the checkout this is run from.
    for (const fs::path& root : {exe.parent_path() / ".." / ".." / "..", tk::find_repo_root()}) {
      if (root.empty() || !fs::is_directory(root / "apps" / "cli")) continue;
      script = fs::weakly_canonical(root / "apps" / "cli" / "dist" / "main.js", ec);
      if (!fs::is_regular_file(script)) {
        std::cerr << "note: the console isn't built (npm install && npm run build:cli); using the basic prompt\n";
        return false;
      }
      break;
    }
  }
  if (script.empty() || !fs::is_regular_file(script)) return false;

  setenv("PAPERBOAT_BIN", exe.c_str(), 1);
  std::vector<std::string> owned = {"node", script.string()};
  owned.insert(owned.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& a : owned) argv.push_back(a.data());
  argv.push_back(nullptr);
  execvp("node", argv.data());
  std::cerr << "note: `node` not found on PATH; using the basic prompt\n";
  return false;
#endif
}

void print_interactive_help() {
  std::cout << "Interactive commands:\n";
  for (const auto& c : command_table()) {
    std::string names(c.name);
    for (const auto alias : c.aliases) names += ", " + std::string(alias);
    std::cout << "  " << std::left << std::setw(18) << names << c.summary << '\n';
  }
  std::cout << "  " << std::left << std::setw(18) << "status" << "Everything running, and the example backends\n"
            << "  " << std::setw(18) << "clear" << "Clear the screen and redraw the banner\n"
            << "  " << std::setw(18) << "help" << "This list (`help <anything>` for the full manual)\n"
            << "  " << std::setw(18) << "exit, quit" << "Leave the prompt\n";
}

int run_interactive_prompt() {
  tui::render_banner(true);
  std::cout << "Interactive prompt. Type 'help' for commands, or 'exit' to quit.\n\n";

  std::string line;
  while (true) {
    if (platform::is_tty(0)) {
      std::cout << "\033[1;38;2;74;144;226mpaperboat\033[0m> " << std::flush;
    }
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      break;
    }

    auto tokens = tui::parse_command_line(line);
    if (tokens.empty()) continue;

    const std::string& cmd = tokens[0];
    if (cmd == "exit" || cmd == "quit" || cmd == "q") {
      break;
    }
    if (cmd == "clear" || cmd == "cls") {
      if (platform::is_tty(1)) std::cout << "\033[2J\033[H";
      tui::render_banner(false);
      continue;
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
      if (tokens.size() == 1) {
        print_interactive_help();
      } else {
        print_help();
      }
      continue;
    }

    std::vector<std::string_view> sv_tokens;
    sv_tokens.reserve(tokens.size());
    for (const auto& t : tokens) sv_tokens.emplace_back(t);

    std::vector<std::string_view> rest(sv_tokens.begin() + 1, sv_tokens.end());
    if (const Command command = find_command(cmd)) command(rest);
    else if (cmd == "status") {
      std::cout << "--- Running Backends ---\n";
      cmd_list({});
      std::cout << "\n--- Example Backends ---\n";
      cmd_examples({"status"});
    } else {
      std::cerr << "unknown command: " << cmd << ". Type 'help' for available commands.\n";
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    launch_console({});
    return run_interactive_prompt();
  }

  std::vector<std::string_view> args(argv + 1, argv + argc);
  std::string_view cmd = args[0];
  std::vector<std::string_view> rest(args.begin() + 1, args.end());

  if (cmd == "interactive" || cmd == "repl") {
    return run_interactive_prompt();
  }
  if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    print_help();
    return 0;
  }
  if (cmd == "--version" || cmd == "-v" || cmd == "version") {
    std::cout << "paperboat " << tui::kVersion << "\n";
    return 0;
  }

  if (const Command command = find_command(cmd)) return command(rest);

  std::cerr << "paperboat: unknown command \"" << cmd << "\"\n";
  std::cerr << "Run 'paperboat --help' for usage.\n";
  return 2;
}
