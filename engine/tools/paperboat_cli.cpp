// The Paperboat unified CLI: build and start backends, list running backends,
// manage the web application, orchestrate examples, and install/browse toolkits.

#include "cli.hpp"

namespace paperboat::cli {

namespace {

void print_help() {
  std::cout << R"(paperboat - Unified Paperboat command-line tool

Usage:
  paperboat <command> [arguments...]

Start and stop (the web app, backends and programs):
  start [target] [options]   Without a target: the web app (dev server, port 3047).
                             A backend (folder, name or .ppk): build it and run it in the foreground.
                             A program from apps.toml (e.g. squero-studio-om): run it.
                               --port <N>      Listen port (backend 8765, web app 3047, squero-studio-om 8790)
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
  examples start [--dir <P>] Launch all example backends (each on its backend.toml port)
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

int cmd_commands(const std::vector<std::string_view>& args);
int cmd_top(const std::vector<std::string_view>&);

}  // namespace

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

namespace {

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

  const fs::path edir = paperboat::cli::find_examples_dir(dir);
  if (sub == "start") {
    return paperboat::cli::start_examples(edir) ? 0 : 1;
  } else if (sub == "stop") {
    return paperboat::cli::stop_examples(edir) ? 0 : 1;
  } else if (sub == "status") {
    auto list = paperboat::cli::list_examples(edir);
    std::cout << std::left << std::setw(8) << "PORT"
              << std::setw(26) << "NAME"
              << std::setw(10) << "TYPE"
              << std::setw(12) << "STATUS"
              << std::setw(8) << "PID"
              << "WS URL\n";
    for (const auto& item : list) {
      std::cout << std::left << std::setw(8) << item.port
                << std::setw(26) << item.name
                << std::setw(10) << item.language
                << std::setw(12) << (item.running ? "running" : "idle")
                << std::setw(8) << (item.running ? std::to_string(item.pid) : "-")
                << "ws://localhost:" << item.port << '\n';
    }
    return 0;
  } else {
    std::cerr << "error: unknown examples subcommand: " << sub << '\n';
    return 2;
  }
}

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
      {"examples", cmd_examples, "Run all example backends", {}, {"start", "stop", "status"}, {"--dir"}},
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

}  // namespace

Command find_command(std::string_view name) {
  for (const auto& c : command_table()) {
    if (c.name == name || std::ranges::find(c.aliases, name) != c.aliases.end()) return c.run;
  }
  return nullptr;
}

namespace {

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

}  // namespace

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

namespace {

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

}  // namespace paperboat::cli

using namespace paperboat::cli;

int main(int argc, char** argv) {
  if (argc < 2) {
    launch_console({});
    return run_interactive_prompt();
  }

  // A bare backend name (`paperboat run cart-pole`) is also looked up among the examples.
  if (const fs::path edir = paperboat::cli::find_examples_dir(); !edir.empty()) {
    core::BackendManager::search_dirs.push_back(edir);
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
