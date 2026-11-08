#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

namespace {

// `--var k=v` for each of the run's variables.
void append_vars(std::vector<std::string>& args, const BackendRunOptions& opts) {
  for (const auto& [k, v] : opts.vars) {
    args.push_back("--var");
    args.push_back(k + "=" + v);
  }
}

}  // namespace

int BackendManager::run_backend(const fs::path& path_or_domain, const BackendRunOptions& opts) {
  fs::path target = fix_pkk_typo(path_or_domain.empty() ? fs::current_path() : path_or_domain);

  // If target does not exist as-is, check if matching archive (.ppk / .paperboat-pkg) exists locally
  if (!fs::exists(target)) {
    std::string stem = target.filename().string();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::current_path(), ec)) {
      if (entry.is_regular_file()) {
        std::string fn = entry.path().filename().string();
        if ((fn == stem + ".ppk" || fn == stem + ".paperboat-pkg" || fn.starts_with(stem + "-")) &&
            (fn.ends_with(".ppk") || fn.ends_with(".paperboat-pkg"))) {
          target = entry.path();
          break;
        }
      }
    }
  }

  // If target is an archive (.ppk or .paperboat-pkg or .pkk), unpack it first to user_backends_dir
  if (is_package(target)) {
    fs::path udir = user_backends_dir();
    std::error_code ec;
    fs::create_directories(udir, ec);
    std::string stem = target.stem().string();
    fs::path dest = udir / stem;
    std::cout << "==> Unpacking package " << target.filename().string() << " to " << dest.string() << "…\n";
    auto unpack_res = unpack_backend(target, dest, /*verify=*/true);
    if (!unpack_res.ok) {
      std::cerr << "error: failed to unpack package: ";
      for (const auto& err : unpack_res.errors) std::cerr << err << "; ";
      std::cerr << '\n';
      return 1;
    }
    target = unpack_res.destination_dir;
  }

  const fs::path dir = resolve_backend_dir(target);
  if (!fs::is_directory(dir)) {
    std::cerr << "error: directory not found: " << path_or_domain << '\n';
    return 1;
  }

  const BackendKind kind = detect_backend_kind(dir);
  if (kind == BackendKind::Unknown) {
    std::cerr << "error: cannot detect backend type (neither CMakeLists.txt nor server.py found) in " << dir << '\n';
    return 1;
  }

  // Determine port
  int port = opts.port;
  BackendManifest manifest = load_manifest(dir);
  if (port <= 0) {
    if (manifest.has_manifest && manifest.port > 0) {
      port = manifest.port;
    } else {
      const auto desc = dir / "descriptor.toml";
      if (fs::is_regular_file(desc)) {
        try {
          auto tbl = toml::parse_file(desc.string());
          if (auto p = tbl["port"].value<int>()) port = *p;
        } catch (...) {}
      }
    }
    if (port <= 0) port = 8765;
  }

  // Free port if already listening
  if (is_port_listening(port)) {
    std::cout << "[notice] port " << port << " is in use, terminating existing listener…\n";
    free_port(port, 2000);
  }

  const std::string domain_name = dir.filename().string();
  // Flushed per line: when stdout is a pipe (the console, a log file) lines still come live.
  auto log_cb = [domain_name](std::string_view stream, std::string_view line) {
    (stream == "stderr" ? std::cerr : std::cout) << "[" << domain_name << "] " << line << std::endl;
  };

  if (kind == BackendKind::Cpp) {
    if (opts.build) {
      std::string err;
      double elapsed = 0.0;
      if (!ensure_built(dir, opts.clean, opts.verbose, &err, &elapsed)) {
        std::cerr << "error: build failed: " << err << '\n';
        return 1;
      }
      if (opts.time_metrics) {
        std::cout << "[timing] build completed in " << elapsed << " s\n";
      }
    }

    // Locate binary in build/
    const fs::path bin = manifest.has_manifest && !manifest.run.empty() && fs::is_regular_file(dir / manifest.run)
                             ? dir / manifest.run
                             : built_binary(dir / "build");
    if (bin.empty()) {
      std::cerr << "error: compiled binary not found in " << (dir / "build") << '\n';
      return 1;
    }

    std::vector<std::string> args = {bin.string(), "--port", std::to_string(port)};
    if (opts.run) args.emplace_back("--run");
    if (!opts.project_file.empty()) {
      args.emplace_back("--project");
      args.emplace_back(opts.project_file);
    }
    append_vars(args, opts);

    std::cout << "paperboat running " << bin.filename().string() << " on " << ws_url(port) << std::endl;
    return ProcessRunner::run_stream(args, dir, {}, log_cb);
  } else {
    // Python backend
    const fs::path repo_root = toolkit::find_repo_root(dir);
    auto py_env = prepare_python_env(repo_root, dir);

    fs::path server_script = dir / "server.py";
    std::string script_run_arg = "server.py";
    if (manifest.has_manifest && !manifest.run.empty() && fs::is_regular_file(dir / manifest.run)) {
      server_script = dir / manifest.run;
      script_run_arg = manifest.run;
    } else if (!fs::is_regular_file(server_script)) {
      std::cerr << "error: server.py not found in " << dir << '\n';
      return 1;
    }

    std::vector<std::string> args = {py_env.python_bin.string(), script_run_arg, "--port", std::to_string(port)};
    if (opts.run) args.emplace_back("--run");
    if (!opts.project_file.empty()) {
      args.emplace_back("--project");
      args.emplace_back(opts.project_file);
    }
    append_vars(args, opts);

    std::cout << "paperboat running python " << server_script.filename().string() << " on " << ws_url(port) << std::endl;
    return ProcessRunner::run_stream(args, dir, py_env.env, log_cb);
  }
}

bool BackendManager::stop_backend(const std::string& target, bool all) {
  auto list = list_running_backends();
  if (list.empty()) {
    std::cout << "No active paperboat backends found.\n";
    return true;
  }

  bool any_stopped = false;
  for (const auto& b : list) {
    bool match = all;
    if (!all) {
      if (std::to_string(b.port) == target) match = true;
      else if (b.domain == target || b.engine == target || b.executable.find(target) != std::string::npos) match = true;
    }

    if (match) {
      std::cout << "Stopping backend on port " << b.port << " (PID " << b.pid << ")…\n";
      free_port(b.port, 2000);
      unregister_running_backend(b.port);
      any_stopped = true;
    }
  }

  if (!all && !any_stopped) {
    // Try stopping as port directly even if not listed in registry
    try {
      int p = std::stoi(target);
      if (p > 0 && is_port_listening(p)) {
        std::cout << "Stopping process on port " << p << "…\n";
        free_port(p, 2000);
        return true;
      }
    } catch (...) {}
    std::cerr << "error: no running backend matched \"" << target << "\"\n";
    return false;
  }

  return true;
}

bool BackendManager::restart_backend(const std::string& target, const BackendRunOptions& opts) {
  auto list = list_running_backends();
  RunningBackend match;
  bool found = false;

  for (const auto& b : list) {
    if (std::to_string(b.port) == target || b.domain == target || b.executable.find(target) != std::string::npos) {
      match = b;
      found = true;
      break;
    }
  }

  fs::path backend_dir;
  int port = 0;
  if (found) {
    backend_dir = match.backend_dir.empty() ? match.cwd : match.backend_dir;
    port = match.port;
    std::cout << "Stopping backend on port " << match.port << "…\n";
    free_port(match.port, 2000);
    unregister_running_backend(match.port);
  } else {
    backend_dir = resolve_backend_dir(target);
    try {
      port = std::stoi(target);
    } catch (...) {}
  }

  if (!fs::is_directory(backend_dir)) {
    std::cerr << "error: cannot locate backend to restart for \"" << target << "\"\n";
    return false;
  }

  BackendRunOptions run_opts = opts;
  if (port > 0 && run_opts.port <= 0) run_opts.port = port;
  return run_backend(backend_dir, run_opts) == 0;
}

}  // namespace paperboat::core
