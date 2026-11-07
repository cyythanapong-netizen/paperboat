// The Paperboat unified CLI: build and start backends, list running backends,
// manage the web application, orchestrate examples, and install/browse toolkits.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/update.hpp"
#include "paperboat/toolkit/installer.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#include "paperboat_tui.hpp"
#ifdef PAPERBOAT_HAS_MODELICA
#include "paperboat/modelica/generate.hpp"
#include "paperboat/modelica/import.hpp"
#include "paperboat/modelica/squero.hpp"
#endif

namespace {

using nlohmann::json;
namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
namespace tui = paperboat::tui;
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

Backend Commands:
  run, start [path | pkg.ppk] [options]
                             Build and run a backend (or .ppk package) in foreground
                             Options:
                               --port <N>      Specify listen port (default: 8765)
                               --no-build      Skip C++ build step
                               --clean         Clean build directory first
                               --time          Print build/startup timing
                               --verbose       Verbose build output
                               --var <key=val> Set default variable value

  build [path] [--clean]     Compile a C++ backend (rebuild: the same with --clean)
  backends [--json]          List the backends you can run (examples, installed)
  list, ps [--json]          List all running Paperboat backends
  restart [path | port]      Restart a running backend
  stop [path | port | --all] Stop a running backend or all backends

Web Application:
  app start [options]        Start Vite web app dev server (default port: 3047)
                               --port <N>      Web server port (default 3047)
                               --host <H>      Listen host (default 0.0.0.0)
                               --daemon        Run in background
  app stop [--port <N>]      Stop web app dev server
  app restart [options]      Restart web app dev server
  app build                  Run production frontend build

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

Modelica (Squero Studio OM, needs OpenModelica):
  modelica generate <Model.mo> [--model <Q.Name>] [--out <model>-cpp/] [--path fmu|ode|auto]
                             Make a backend from a Modelica model (ODE when it can, else FMU)
  modelica check <Model.mo> [--model <Q.Name>]
                             omc's checkModel on it
  modelica icon <Model.mo | Q.Name> [--class <Name>]
                             Import a Modelica class's icon and connectors as JSON
  squero [Model.mo] [--port <N>] [--app <dir>] [--version <V>]
                             Squero Studio OM: the squero/0 server and the app (squero-studio-om,
                             fetched on first use; default port 8790); opens Model.mo
                             (default ./Untitled.mo, created if missing)

Updating:
  update [--check] [--no-build]
                             Pull this repository (fast-forward only), rebuild the engine and console
                             when it changed, pull the sibling checkouts of the apps in apps.toml and
                             rebuild them, and refresh installed apps. --check only reports.

Packages (apps from other repositories, see apps.toml):
  pkg list [--json]          The apps, installed or not
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

int cmd_list(const std::vector<std::string_view>& args) {
  bool json_out = false;
  for (auto a : args) {
    if (a == "--json") json_out = true;
  }

  auto list = core::BackendManager::list_running_backends();
  if (json_out) {
    json arr = json::array();
    for (const auto& b : list) {
      arr.push_back({
          {"port", b.port},
          {"pid", b.pid},
          {"engine", b.engine},
          {"domain", b.domain},
          {"ws_url", b.ws_url},
          {"cwd", b.cwd.string()},
          {"backend_dir", b.backend_dir.string()},
          {"started_at", b.started_at},
      });
    }
    std::cout << arr.dump(2) << '\n';
    return 0;
  }

  if (list.empty()) {
    std::cout << "No running Paperboat backends.\n";
    return 0;
  }

  std::cout << std::left << std::setw(8) << "PORT"
            << std::setw(8) << "PID"
            << std::setw(28) << "ENGINE"
            << std::setw(18) << "DOMAIN"
            << "WS URL" << '\n';

  for (const auto& b : list) {
    std::cout << std::left << std::setw(8) << b.port
              << std::setw(8) << b.pid
              << std::setw(28) << b.engine
              << std::setw(18) << b.domain
              << b.ws_url << '\n';
  }
  return 0;
}

int cmd_restart(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "error: specify backend path or port to restart (e.g. `paperboat restart 8765`)\n";
    return 2;
  }
  core::BackendRunOptions opts;
  std::string target(args[0]);
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--port" && i + 1 < args.size()) {
      opts.port = std::stoi(std::string(args[++i]));
    }
  }
  return core::BackendManager::restart_backend(target, opts) ? 0 : 1;
}

int cmd_stop(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "error: specify backend path, port, or --all (e.g. `paperboat stop 8765`)\n";
    return 2;
  }
  bool all = false;
  std::string target;
  for (auto a : args) {
    if (a == "--all") all = true;
    else if (!a.starts_with("-") && target.empty()) target = std::string(a);
  }
  if (!all && target.empty()) {
    std::cerr << "error: specify backend path, port, or --all\n";
    return 2;
  }
  return core::BackendManager::stop_backend(target, all) ? 0 : 1;
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
    std::cerr << "usage: paperboat examples <start|stop|status> [--dir <path>]\n";
    return 2;
  }
  auto sub = args[0];
  std::string dir;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--dir" && i + 1 < args.size()) {
      dir = std::string(args[++i]);
    }
  }

  if (sub == "start") {
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
                       {"installed", a.installed}, {"version", a.version}, {"deps", a.deps}});
      std::cout << arr.dump(2) << '\n';
      return 0;
    }
    if (list.empty()) std::cerr << "no apps (apps.toml not found in the repository root)\n";
    for (const auto& a : list)
      std::cout << std::left << std::setw(20) << a.name << std::setw(14) << (a.installed ? a.version : "not installed")
                << a.description << '\n';
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
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  for (p = p.parent_path(); !ec && p != p.root_path() && p.has_parent_path(); p = p.parent_path())
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
  sync("paperboat", root, changed);
  for (const auto& app : tk::list_apps(root)) {  // after the pull, so a new apps.toml counts
    const fs::path dir = root.parent_path() / app.name;
    if (!fs::exists(dir / ".git")) continue;  // not checked out here: `pkg` handles the installed copy
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

#ifdef PAPERBOAT_HAS_MODELICA
namespace mo = paperboat::modelica;
#endif

constexpr const char* kNoModelica =
    "paperboat was built without engine/modelica: install OpenModelica (omc on PATH) and rebuild\n";

// paperboat modelica generate <Model.mo> [--model Q.Name] [--out dir] [--path fmu|ode|auto]
// paperboat modelica check <Model.mo> [--model Q.Name]
// paperboat modelica icon <Model.mo|Qualified.Name> [--class X]
int cmd_modelica(const std::vector<std::string_view>& args) {
#ifndef PAPERBOAT_HAS_MODELICA
  (void)args;
  std::cerr << kNoModelica;
  return 1;
#else
  if (args.size() < 2 || (args[0] != "generate" && args[0] != "check" && args[0] != "icon")) {
    std::cerr << "usage: paperboat modelica generate <Model.mo> [--model <Qualified.Name>] [--out <model>-cpp/] "
                 "[--path fmu|ode|auto]\n"
                 "       paperboat modelica check <Model.mo> [--model <Qualified.Name>]\n"
                 "       paperboat modelica icon <Model.mo|Qualified.Name> [--class <Name>]\n";
    return 2;
  }
  if (args[0] == "icon") {
    const std::string_view target = args[1];
    std::string cls;
    for (std::size_t i = 2; i < args.size(); ++i) {
      const auto flag = args[i];
      if (flag == "--json") continue;
      if (i + 1 >= args.size()) {
        std::cerr << "error: " << flag << " needs a value\n";
        return 2;
      }
      const std::string value(args[++i]);
      if (flag == "--class" || flag == "--model") cls = value;
      else {
        std::cerr << "error: unknown option " << flag << '\n';
        return 2;
      }
    }
    try {
      mo::Omc omc;
      const bool is_file = fs::is_regular_file(target) || target.ends_with(".mo") ||
                           target.find('/') != std::string_view::npos || target.find('\\') != std::string_view::npos;
      const mo::ImportedModel imp = is_file ? mo::import_model(omc, fs::path(target), cls)
                                            : mo::import_model(omc, {}, cls.empty() ? target : std::string_view(cls));
      std::cout << imp.to_json().dump(2) << '\n';
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "error: " << e.what() << '\n';
      return 1;
    }
  }
  mo::GenerateOptions o{.source = std::string(args[1])};
  for (std::size_t i = 2; i < args.size(); ++i) {
    const auto flag = args[i];
    if (i + 1 >= args.size()) {
      std::cerr << "error: " << flag << " needs a value\n";
      return 2;
    }
    const std::string value(args[++i]);
    if (flag == "--model") o.model = value;
    else if (flag == "--out") o.out = value;
    else if (flag == "--path") o.path = value;
    else {
      std::cerr << "error: unknown option " << flag << '\n';
      return 2;
    }
  }
  try {
    mo::Omc omc;
    if (args[0] == "check") {
      const auto r = mo::check(omc, o.source, o.model);
      (r.ok ? std::cout : std::cerr) << r.message << '\n';
      return r.ok ? 0 : 1;
    }
    const auto r = mo::generate(omc, o);
    std::cout << r.model << " -> " << r.out.string() << "/ (" << r.reason << ")\n";
    for (const auto& n : r.notices) std::cout << "  note: " << n << '\n';
    for (const auto& f : r.written) std::cout << "  wrote " << f.string() << '\n';
    std::cout << "next: paperboat build " << r.out.string() << " && paperboat run " << r.out.string() << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
#endif
}

// paperboat squero [start|run] [Model.mo] [--port N] [--app dir] [--version V]: Squero Studio OM, the
// squero/0 server and the Studio app on one port. The app is --app, else a squero-studio-om checkout
// next to this repo (its dist/, for developing it), else the one `paperboat pkg` installed, fetched
// on first use (--version V fetches that release).
int cmd_squero(const std::vector<std::string_view>& args) {
#ifndef PAPERBOAT_HAS_MODELICA
  (void)args;
  std::cerr << kNoModelica;
  return 1;
#else
  constexpr const char* kApp = "squero-studio-om";
  int port = 8790;
  fs::path app;
  fs::path file;
  std::string version;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--port" && i + 1 < args.size()) port = std::stoi(std::string(args[++i]));
    else if (args[i] == "--app" && i + 1 < args.size()) app = std::string(args[++i]);
    else if (args[i] == "--version" && i + 1 < args.size()) version = std::string(args[++i]);
    else if (i == 0 && (args[i] == "start" || args[i] == "run")) continue;
    else if (!args[i].starts_with("--")) file = std::string(args[i]);
  }
  if (version.starts_with('v')) version.erase(0, 1);
  if (app.empty()) {
    const fs::path root = tk::find_repo_root();
    const fs::path sibling = root.empty() ? fs::path{} : root.parent_path() / kApp / "dist";
    if (version.empty() && fs::is_regular_file(sibling / "index.html")) {
      app = sibling;
    } else {
      std::string have;
      for (const auto& a : tk::list_apps()) if (a.name == kApp && a.installed) have = a.version;
      if (have.empty() || (!version.empty() && version != have)) {
        const auto r = tk::install_app(kApp, version, {}, [](std::string_view, std::string_view t) {
          std::cerr << "squero: " << t << '\n';
        });
        if (!r.ok) std::cerr << "squero: " << r.message << '\n';
      }
      app = tk::app_output(kApp);
      if (app.empty()) std::cerr << "note: Studio isn't installed (paperboat pkg install " << kApp << ", or pass --app); serving squero/0 only\n";
    }
  }
  return mo::serve_squero(port, app, {}, file);
#endif
}

int cmd_commands(const std::vector<std::string_view>& args);
int cmd_top(const std::vector<std::string_view>&);

using Command = int (*)(const std::vector<std::string_view>&);

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
      {"run", cmd_run, "Build and run a backend", {"start"}, {},
       {"--port", "--no-build", "--clean", "--time", "--verbose", "--var"}, "backend"},
      {"open", cmd_open, "Open a saved project file (.ppj, .paperboat.json)", {}, {},
       {"--run", "--port", "--no-build"}, "project"},
      {"build", cmd_build, "Compile a C++ backend", {}, {}, {"--clean"}, "backend"},
      {"rebuild", cmd_rebuild, "Clean and compile a C++ backend", {}, {}, {}, "backend"},
      {"backends", cmd_backends, "List the backends you can run", {}, {}, {"--json"}},
      {"list", cmd_list, "List running backends", {"ps"}, {}, {"--json"}},
      {"restart", cmd_restart, "Restart a running backend", {}, {}, {"--port"}, "target"},
      {"stop", cmd_stop, "Stop a running backend", {}, {}, {"--all"}, "target"},
      {"app", cmd_app, "Manage the web app dev server", {}, {"start", "stop", "restart", "build"},
       {"--port", "--host", "--daemon"}},
      {"examples", cmd_examples, "Run all example backends", {}, {"start", "stop", "status"}, {"--dir"}},
      {"toolkit", cmd_toolkit, "Manage optional toolkits", {}, {"list", "install", "update", "uninstall", "versions"},
       {"--json", "--all", "--no-build", "--version"}, "toolkit"},
      {"check", cmd_check, "Verify a backend (manifest, catalog, smoke, golden)", {}, {},
       {"--fast", "--json", "--json-stream"}, "backend"},
      {"pack", cmd_pack, "Package a backend as .ppk", {}, {}, {"--out", "--json-stream"}, "backend"},
      {"unpack", cmd_unpack, "Unpack and verify a .ppk", {}, {}, {"--dir", "--json-stream"}, "ppk"},
      {"modelica", cmd_modelica, "Make a backend from a Modelica model", {}, {"generate", "check", "icon"},
       {"--model", "--out", "--path", "--class"}, "file"},
      {"squero", cmd_squero, "Run Squero Studio OM (server and app)", {"studio"}, {"start", "run"},
       {"--port", "--app", "--version"}, "file"},
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
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || std::getenv("PAPERBOAT_NO_CONSOLE")) return false;
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
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
}

void print_interactive_help() {
  std::cout << R"(Interactive Commands:
  run, start <target>      Build and start backend (or .ppk package)
  build, rebuild <target>  Compile a C++ backend (rebuild cleans first)
  backends                 List the backends you can run
  list (or ps)             List running Paperboat backends
  restart <target>         Restart backend on port or name
  stop <target|--all>      Stop running backend
  app <start|stop|build>   Manage the web application dev server
  examples <start|stop|status>
                           Manage example backends (ports 8765-8773)
  toolkit <list|install|update|versions>
                           Manage optional toolkits (e.g. `toolkit install opencv`)
  pkg <list|install|update|uninstall|versions|path>
                           Apps from other repositories (e.g. `pkg install squero-studio-om`)
  update [--check]         Pull this repository and its app checkouts, rebuild
  check [backend] [--fast] 4-stage backend verification (manifest, catalog, smoke, golden)
  pack [folder] [--out <pkg>]
                           Package backend into .ppk archive
  unpack <pkg> [--dir <dir>]
                           Unpack and verify .ppk archive
  status                   Show overall status of backends and examples
  clear                    Clear screen and redraw banner
  help                     Show this help message
  exit (or quit)           Exit interactive prompt
)";
}

int run_interactive_prompt() {
  tui::render_banner(true);
  std::cout << "Interactive prompt. Type 'help' for commands, or 'exit' to quit.\n\n";

  std::string line;
  while (true) {
    if (isatty(STDIN_FILENO)) {
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
      if (isatty(STDOUT_FILENO)) std::cout << "\033[2J\033[H";
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
    std::cout << "paperboat 0.1.0\n";
    return 0;
  }

  if (const Command command = find_command(cmd)) return command(rest);

  std::cerr << "paperboat: unknown command \"" << cmd << "\"\n";
  std::cerr << "Run 'paperboat --help' for usage.\n";
  return 2;
}
