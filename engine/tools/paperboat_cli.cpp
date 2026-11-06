// The Paperboat unified CLI: build and start backends, list running backends,
// manage the web application, orchestrate examples, and install/browse toolkits.

#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/installer.hpp"
#include "paperboat_tui.hpp"

namespace {

using nlohmann::json;
namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
namespace tui = paperboat::tui;

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
  toolkit list [--json]      List all available and installed toolkits
  toolkit install <name>     Install an optional toolkit (e.g. opencv)
  toolkit update <name>      Update an installed toolkit
  toolkit uninstall <name>   Remove an optional toolkit
  toolkit versions <name>    Query remote versions/tags for a toolkit

Verification & Packaging:
  check [path] [--fast] [--json]
                             4-stage verification (manifest, catalog, smoke, golden)
  pack [path] [--out <pkg>]  Package backend into .ppk archive
  unpack <pkg> [--dir <dir>] Unpack and verify .ppk archive

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

  return core::BackendManager::run_backend(path, opts);
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
    std::cerr << "usage: paperboat toolkit <list|install|update|uninstall|versions> [args...]\n";
    return 2;
  }
  auto sub = args[0];
  bool json_out = false;
  bool rebuild = true;
  std::string name;
  std::string version;

  for (std::size_t i = 1; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--json") json_out = true;
    else if (a == "--no-build") rebuild = false;
    else if ((a == "--version" || a == "-v") && i + 1 < args.size()) {
      version = std::string(args[++i]);
    }
    else if (!a.starts_with("-") && name.empty()) name = std::string(a);
  }

  if (sub == "list") {
    auto list = tk::list_toolkits();
    if (json_out) {
      std::cout << tk::toolkits_json(list) << '\n';
      return 0;
    }
    for (const auto& t : list) {
      std::cout << t.name << "\t" << (t.installed ? "installed" : "not installed") << "\t" << t.repo;
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

  if (name.empty()) {
    std::cerr << "error: toolkit name required\n";
    return 2;
  }

  auto log = [](std::string_view level, std::string_view text) {
    (level == "error" ? std::cerr : std::cout) << "[" << level << "] " << text << '\n';
  };

  tk::HostBuild host{.source_dir = {}, .build_dir = {}, .skip_rebuild = !rebuild};
  tk::ToolkitResult res;
  if (sub == "install" || sub == "add") {
    res = tk::install_toolkit(name, version, {}, host, log);
  } else if (sub == "update") {
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

int cmd_check(const std::vector<std::string_view>& args) {
  std::string path;
  bool fast = false;
  bool json_out = false;

  for (auto a : args) {
    if (a == "--fast") fast = true;
    else if (a == "--json") json_out = true;
    else if (!a.starts_with("-") && path.empty()) path = std::string(a);
  }

  auto res = core::BackendManager::check_backend(path, fast);
  if (json_out) {
    json steps_json = json::array();
    for (const auto& s : res.steps) {
      steps_json.push_back({
          {"name", s.name},
          {"ok", s.ok},
          {"skipped", s.skipped},
          {"message", s.message},
          {"duration", s.duration_seconds},
      });
    }
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
    std::cout << doc.dump(2) << '\n';
    return res.ok ? 0 : 1;
  }

  std::cout << "backend \"" << (res.name.empty() ? res.domain : res.name)
            << "\" (" << (res.version.empty() ? "0.1.0" : res.version)
            << ", " << (res.language.empty() ? "unknown" : res.language) << "):\n";

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

int cmd_pack(const std::vector<std::string_view>& args) {
  std::string path;
  std::string out_path;

  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--out" && i + 1 < args.size()) {
      out_path = std::string(args[++i]);
    } else if (!a.starts_with("-") && path.empty()) {
      path = std::string(a);
    }
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

  for (std::size_t i = 0; i < args.size(); ++i) {
    auto a = args[i];
    if (a == "--dir" && i + 1 < args.size()) {
      dest_dir = std::string(args[++i]);
    } else if (!a.starts_with("-") && archive.empty()) {
      archive = std::string(a);
    }
  }

  if (archive.empty()) {
    std::cerr << "error: package archive path required (e.g. `paperboat unpack my-backend-0.1.0.ppk`)\n";
    return 2;
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

void print_interactive_help() {
  std::cout << R"(Interactive Commands:
  run, start <target>      Build and start backend (or .ppk package)
  list (or ps)             List running Paperboat backends
  restart <target>         Restart backend on port or name
  stop <target|--all>      Stop running backend
  app <start|stop|build>   Manage the web application dev server
  examples <start|stop|status>
                           Manage example backends (ports 8765-8773)
  toolkit <list|install|update|versions>
                           Manage optional toolkits (e.g. `toolkit install opencv`)
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
    if (cmd == "run" || cmd == "start") cmd_run(rest);
    else if (cmd == "list" || cmd == "ps") cmd_list(rest);
    else if (cmd == "restart") cmd_restart(rest);
    else if (cmd == "stop") cmd_stop(rest);
    else if (cmd == "app") cmd_app(rest);
    else if (cmd == "examples") cmd_examples(rest);
    else if (cmd == "toolkit") cmd_toolkit(rest);
    else if (cmd == "check") cmd_check(rest);
    else if (cmd == "pack") cmd_pack(rest);
    else if (cmd == "unpack") cmd_unpack(rest);
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

  if (cmd == "run" || cmd == "start") return cmd_run(rest);
  if (cmd == "list" || cmd == "ps") return cmd_list(rest);
  if (cmd == "restart") return cmd_restart(rest);
  if (cmd == "stop") return cmd_stop(rest);
  if (cmd == "app") return cmd_app(rest);
  if (cmd == "examples") return cmd_examples(rest);
  if (cmd == "toolkit") return cmd_toolkit(rest);
  if (cmd == "check") return cmd_check(rest);
  if (cmd == "pack") return cmd_pack(rest);
  if (cmd == "unpack") return cmd_unpack(rest);

  std::cerr << "paperboat: unknown command \"" << cmd << "\"\n";
  std::cerr << "Run 'paperboat --help' for usage.\n";
  return 2;
}
