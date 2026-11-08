// Programs and apps (apps.toml), packages, updates and toolkits.

#include "cli.hpp"

namespace paperboat::cli {

namespace {

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

}  // namespace

// An app from apps.toml (squero-studio-om): `target` is the name, or the command that launches it (`squero`).
// A path or a ./relative name is never one: that is a backend folder.
std::optional<tk::AppEntry> find_program(std::string_view target) {
  if (target.empty() || target.starts_with('.') || target.find_first_of("/\\") != std::string_view::npos) return std::nullopt;
  for (auto& app : tk::list_apps())
    if (app.name == target || (!app.launch.empty() && app.launch == target)) return app;
  return std::nullopt;
}

// `start`, `stop` and `restart` with no target are for the main app (the web app); `app <verb>` is the same.
int main_app(std::string_view verb, const std::vector<std::string_view>& args) {
  std::vector<std::string_view> forwarded = {verb};
  forwarded.insert(forwarded.end(), args.begin(), args.end());
  return cmd_app(forwarded);
}

namespace {

// --port N, else the program's own port from apps.toml.
int port_of(const tk::AppEntry& app, const std::vector<std::string_view>& args) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == "--port") return std::stoi(std::string(args[i + 1]));
  return app.port;
}

}  // namespace

int stop_program(tk::AppEntry app, const std::vector<std::string_view>& args) {
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

namespace {

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

}  // namespace

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

namespace {

// The build directory of the running binary (engine/build/dev, engine/build, ...).
fs::path running_build_dir(const fs::path& root) {
  fs::path p = platform::exe_path();
  for (p = p.parent_path(); !p.empty() && p != p.root_path() && p.has_parent_path(); p = p.parent_path())
    if (fs::is_regular_file(p / "CMakeCache.txt")) return p;
  return root / "engine" / "build";
}

}  // namespace

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
      if (tk::run_command({"npm", "view", "@paperboat/cli", "version"}, {}, {}, &latest) != 0) {
        say("paperboat", std::string(tui::kVersion) + ", cannot read @paperboat/cli from the npm registry (offline, or not published yet)");
        ++failed;
      } else if (latest == tui::kVersion) {
        say("paperboat", std::string(tui::kVersion) + ", up to date (npm)");
      } else {
        say("paperboat", std::string(tui::kVersion) + ", " + latest + " is available (npm)");
        if (!check_only) step("npm install -g @paperboat/cli@latest", {"npm", "install", "-g", "@paperboat/cli@latest"}, root);
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

}  // namespace paperboat::cli
