// The backend commands: run, start, stop, restart, list, backends, build, check, pack, unpack, open.

#include "cli.hpp"

namespace paperboat::cli {

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

namespace {

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

}  // namespace

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
  for (const auto& ex : paperboat::cli::list_examples(paperboat::cli::find_examples_dir())) add(ex.dir, "example", ex.port);
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

namespace {

// The first sentence of an app's description, for the one-line listings.
std::string first_sentence(std::string text) {
  if (const auto dot = text.find(". "); dot != std::string::npos) text.resize(dot);
  if (!text.empty() && text.back() == '.') text.pop_back();
  return text;
}

constexpr int kWebAppPort = 3047;

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

}  // namespace

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

namespace {

json step_json(const core::CheckStep& s) {
  return {{"name", s.name}, {"ok", s.ok}, {"skipped", s.skipped}, {"message", s.message}, {"duration", s.duration_seconds}};
}

// One line of `--json-stream`: an event object, flushed so a reader sees it at once.
void emit(json event) {
  std::cout << event.dump() << std::endl;
}

}  // namespace

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

namespace {

core::PackProgress progress_events() {
  return [](int files, int total, uint64_t bytes) {
    emit({{"event", "progress"}, {"files", files}, {"total", total}, {"bytes", bytes}});
  };
}

}  // namespace

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

}  // namespace paperboat::cli
