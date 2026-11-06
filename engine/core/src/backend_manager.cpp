#include "paperboat/core/backend_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/installer.hpp"

namespace paperboat::core {
namespace {

using nlohmann::json;
namespace fs = std::filesystem;

int64_t current_timestamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

bool BackendManager::is_port_listening(int port) {
  if (port <= 0 || port > 65535) return false;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return false;

  // Set non-blocking for quick check
  fcntl(sock, F_SETFL, O_NONBLOCK);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (res == 0) {
    close(sock);
    return true;
  }

  if (errno == EINPROGRESS) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    timeval tv{.tv_sec = 0, .tv_usec = 30000};  // 30 ms
    if (select(sock + 1, nullptr, &fds, nullptr, &tv) > 0) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
      close(sock);
      return err == 0;
    }
  }

  close(sock);
  return false;
}

int BackendManager::find_pid_by_port(int port) {
  if (port <= 0) return 0;

  // Search socket inode in /proc/net/tcp and /proc/net/tcp6
  std::string hex_port;
  {
    std::stringstream ss;
    ss << std::uppercase << std::hex << port;
    hex_port = ss.str();
    while (hex_port.size() < 4) hex_port = "0" + hex_port;
  }

  std::vector<std::string> socket_inodes;
  for (const char* netfile : {"/proc/net/tcp", "/proc/net/tcp6"}) {
    std::ifstream in(netfile);
    if (!in) continue;
    std::string line;
    // skip header
    std::getline(in, line);
    while (std::getline(in, line)) {
      std::istringstream iss(line);
      std::string sl, local_addr, rem_addr, st, tx_rx, tr, retr, uid, timeout, inode;
      if (iss >> sl >> local_addr >> rem_addr >> st >> tx_rx >> tr >> retr >> uid >> timeout >> inode) {
        if (st == "0A" && local_addr.ends_with(":" + hex_port)) {
          socket_inodes.push_back(inode);
        }
      }
    }
  }

  if (!socket_inodes.empty()) {
    DIR* proc = opendir("/proc");
    if (proc) {
      dirent* entry;
      while ((entry = readdir(proc)) != nullptr) {
        if (entry->d_type == DT_DIR && std::isdigit(entry->d_name[0])) {
          int pid = std::atoi(entry->d_name);
          std::string fd_dir = std::string("/proc/") + entry->d_name + "/fd";
          DIR* fddir = opendir(fd_dir.c_str());
          if (fddir) {
            dirent* fdentry;
            while ((fdentry = readdir(fddir)) != nullptr) {
              std::string linkpath = fd_dir + "/" + fdentry->d_name;
              std::array<char, 256> buf{};
              ssize_t len = readlink(linkpath.c_str(), buf.data(), buf.size() - 1);
              if (len > 0) {
                buf[static_cast<std::size_t>(len)] = '\0';
                std::string target(buf.data());
                for (const auto& inode : socket_inodes) {
                  if (target == "socket:[" + inode + "]") {
                    closedir(fddir);
                    closedir(proc);
                    return pid;
                  }
                }
              }
            }
            closedir(fddir);
          }
        }
      }
      closedir(proc);
    }
  }

  // Fallback using fuser
  auto res = ProcessRunner::run_sync({"fuser", std::to_string(port) + "/tcp"});
  if (res.exit_code == 0 && !res.stdout_text.empty()) {
    std::istringstream iss(res.stdout_text);
    int pid = 0;
    if (iss >> pid && pid > 0) return pid;
  }

  return 0;
}

bool BackendManager::stop_user_service(const std::string& unit_name) {
  if (unit_name.empty()) return true;
  ProcessRunner::run_sync({"systemctl", "--user", "stop", unit_name});
  ProcessRunner::run_sync({"systemctl", "--user", "reset-failed", unit_name});
  return true;
}

bool BackendManager::free_port(int port, int timeout_ms) {
  if (!is_port_listening(port)) return true;

  // Stop corresponding systemd service if any
  stop_user_service("paperboat-backend-" + std::to_string(port) + ".service");
  if (port == 3047) stop_user_service("paperboat-web.service");

  int pid = find_pid_by_port(port);
  if (pid > 0) {
    ProcessRunner::kill_pid(pid, SIGTERM, timeout_ms / 2);
  }

  // Fallback to fuser -k
  if (is_port_listening(port)) {
    ProcessRunner::run_sync({"fuser", "-k", std::to_string(port) + "/tcp"});
  }

  auto start = std::chrono::steady_clock::now();
  while (is_port_listening(port)) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() >= timeout_ms) {
      int late_pid = find_pid_by_port(port);
      if (late_pid > 0) ProcessRunner::kill_pid(late_pid, SIGKILL, 200);
      return !is_port_listening(port);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

fs::path BackendManager::runtime_dir() {
  return fs::path("/tmp/paperboat-runtime");
}

fs::path BackendManager::runtime_record_path(int port) {
  return runtime_dir() / ("backend-" + std::to_string(port) + ".json");
}

void BackendManager::register_running_backend(const RunningBackend& backend) {
  std::error_code ec;
  fs::create_directories(runtime_dir(), ec);
  const auto p = runtime_record_path(backend.port);

  json doc = {
      {"port", backend.port},
      {"pid", backend.pid},
      {"engine", backend.engine},
      {"domain", backend.domain},
      {"cwd", backend.cwd.string()},
      {"backend_dir", backend.backend_dir.string()},
      {"executable", backend.executable},
      {"ws_url", backend.ws_url.empty() ? ("ws://localhost:" + std::to_string(backend.port)) : backend.ws_url},
      {"started_at", backend.started_at > 0 ? backend.started_at : current_timestamp()}};

  std::ofstream out(p);
  if (out) out << doc.dump(2) << '\n';
}

void BackendManager::unregister_running_backend(int port) {
  std::error_code ec;
  fs::remove(runtime_record_path(port), ec);
}

std::vector<RunningBackend> BackendManager::list_running_backends() {
  std::vector<RunningBackend> result;
  std::vector<int> found_ports;
  const auto rdir = runtime_dir();

  if (fs::is_directory(rdir)) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(rdir, ec)) {
      if (!entry.is_regular_file()) continue;
      const auto path = entry.path();
      if (!path.filename().string().starts_with("backend-") || path.extension() != ".json") continue;

      try {
        std::ifstream in(path);
        if (!in) continue;
        auto doc = json::parse(in);
        int port = doc.value("port", 0);
        int pid = doc.value("pid", 0);

        if (port <= 0 || pid <= 0 || !ProcessRunner::is_alive(pid) || !is_port_listening(port)) {
          // Prune stale file
          std::error_code rm_ec;
          fs::remove(path, rm_ec);
          continue;
        }

        RunningBackend b;
        b.port = port;
        b.pid = pid;
        b.engine = doc.value("engine", "");
        b.domain = doc.value("domain", "");
        b.cwd = doc.value("cwd", "");
        b.backend_dir = doc.value("backend_dir", "");
        b.executable = doc.value("executable", "");
        b.ws_url = doc.value("ws_url", "ws://localhost:" + std::to_string(port));
        b.started_at = doc.value("started_at", int64_t{0});

        result.push_back(std::move(b));
        found_ports.push_back(port);
      } catch (...) {
        std::error_code rm_ec;
        fs::remove(path, rm_ec);
      }
    }
  }

  // Fallback probe for known example ports 8765..8773 (and any from 8765 to 8780)
  for (int p = 8765; p <= 8780; ++p) {
    if (std::ranges::find(found_ports, p) != found_ports.end()) continue;
    if (is_port_listening(p)) {
      int pid = find_pid_by_port(p);
      if (pid > 0) {
        std::string cmd = ProcessRunner::read_cmdline(pid);
        RunningBackend b;
        b.port = p;
        b.pid = pid;
        b.ws_url = "ws://localhost:" + std::to_string(p);
        b.executable = cmd;

        if (cmd.find("cart-pole") != std::string::npos) {
          b.domain = "cart-pole";
          b.engine = "paperboat/cart-pole-cpp";
        } else if (cmd.find("parts") != std::string::npos) {
          b.domain = "parts";
          b.engine = "paperboat/parts-cpp";
        } else if (cmd.find("double-pendulum") != std::string::npos) {
          b.domain = "double-pendulum";
          b.engine = "paperboat/double-pendulum-cpp";
        } else if (cmd.find("mass-spring") != std::string::npos) {
          b.domain = "mass-spring";
          b.engine = "python";
        } else if (cmd.find("runner") != std::string::npos) {
          b.domain = "runner";
          b.engine = "python";
        } else if (cmd.find("animation") != std::string::npos) {
          b.domain = "animation";
          b.engine = "python";
        } else if (cmd.find("chain") != std::string::npos) {
          b.domain = "chain";
          b.engine = "python";
        } else if (cmd.find("supply") != std::string::npos) {
          b.domain = "supply";
          b.engine = "python";
        } else if (cmd.find("parametric") != std::string::npos) {
          b.domain = "parametric";
          b.engine = "python";
        } else {
          b.domain = "unknown";
          b.engine = "paperboat";
        }

        result.push_back(std::move(b));
      }
    }
  }

  std::ranges::sort(result, [](const RunningBackend& a, const RunningBackend& b) {
    return a.port < b.port;
  });
  return result;
}

bool BackendManager::needs_rebuild(const fs::path& backend_dir, const fs::path& build_dir) {
  const fs::path bdir = build_dir.empty() ? (backend_dir / "build") : build_dir;
  const fs::path stamp_file = bdir / ".paperboat-build.stamp";

  if (!fs::exists(bdir / "CMakeCache.txt") || !fs::is_regular_file(stamp_file)) {
    return true;
  }

  // Find compiled binary in build_dir
  bool has_bin = false;
  std::filesystem::file_time_type bin_time;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(bdir, ec)) {
    if (entry.is_regular_file()) {
      auto name = entry.path().filename().string();
      if (name.starts_with("paperboat-") && !name.ends_with(".o") && !name.ends_with(".a")) {
        has_bin = true;
        bin_time = fs::last_write_time(entry.path(), ec);
        break;
      }
    }
  }
  if (!has_bin) return true;

  auto stamp_time = fs::last_write_time(stamp_file, ec);
  if (stamp_time > bin_time) stamp_time = bin_time;

  // Scan sources for changes
  auto check_newer = [&](const fs::path& p) {
    if (fs::exists(p) && fs::last_write_time(p, ec) > stamp_time) return true;
    return false;
  };

  if (check_newer(backend_dir / "CMakeLists.txt")) return true;
  if (check_newer(backend_dir / "descriptor.toml")) return true;
  if (check_newer(backend_dir / "backend.toml")) return true;

  for (const auto& entry : fs::recursive_directory_iterator(backend_dir, ec)) {
    if (!entry.is_regular_file()) continue;
    const auto& p = entry.path();
    const auto rel = p.lexically_relative(backend_dir);
    if (!rel.empty() && *rel.begin() == "build") continue;
    auto ext = p.extension().string();
    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".toml") {
      if (fs::last_write_time(p, ec) > stamp_time) return true;
    }
  }

  return false;
}

bool BackendManager::ensure_built(const fs::path& backend_dir,
                                  bool clean,
                                  bool verbose,
                                  std::string* error_msg,
                                  double* elapsed_seconds) {
  auto start_time = std::chrono::steady_clock::now();
  const fs::path build_dir = backend_dir / "build";
  std::error_code ec;

  if (clean) {
    fs::remove_all(build_dir, ec);
  }

  if (!needs_rebuild(backend_dir, build_dir)) {
    if (verbose) std::cout << "[build] up to date (skipped)\n";
    if (elapsed_seconds) {
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start_time);
      *elapsed_seconds = static_cast<double>(duration.count()) / 1000000.0;
    }
    return true;
  }

  if (!fs::exists(build_dir / "CMakeCache.txt")) {
    if (verbose) std::cout << "[build] configuring with cmake…\n";
    auto config_res = ProcessRunner::run_sync({"cmake", "-S", backend_dir.string(), "-B", build_dir.string()});
    if (!config_res.ok()) {
      if (error_msg) *error_msg = "cmake configure failed: " + config_res.stderr_text;
      return false;
    }
  }

  if (verbose) std::cout << "[build] compiling backend…\n";
  auto build_res = ProcessRunner::run_sync({"cmake", "--build", build_dir.string()});
  if (!build_res.ok()) {
    if (error_msg) *error_msg = "cmake build failed: " + build_res.stderr_text;
    return false;
  }

  // Update stamp file
  {
    std::ofstream stamp(build_dir / ".paperboat-build.stamp");
    stamp << current_timestamp() << '\n';
  }

  if (elapsed_seconds) {
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time);
    *elapsed_seconds = static_cast<double>(duration.count()) / 1000000.0;
  }
  return true;
}

BackendManager::PythonEnv BackendManager::prepare_python_env(const fs::path& repo_root,
                                                            const fs::path& backend_dir) {
  PythonEnv env;
  const fs::path root = repo_root.empty() ? toolkit::find_repo_root(backend_dir) : repo_root;

  // 1. Resolve python binary: backend venv -> root venv -> system python3
  if (!backend_dir.empty() && fs::is_regular_file(backend_dir / ".venv" / "bin" / "python3")) {
    env.python_bin = backend_dir / ".venv" / "bin" / "python3";
  } else if (!root.empty() && fs::is_regular_file(root / ".venv" / "bin" / "python3")) {
    env.python_bin = root / ".venv" / "bin" / "python3";
  } else {
    env.python_bin = "python3";
  }

  // 2. Set PYTHONPATH to root/python
  if (!root.empty()) {
    std::string pythonpath = (root / "python").string();
    const char* cur = std::getenv("PYTHONPATH");
    if (cur && *cur) {
      pythonpath += ":" + std::string(cur);
    }
    env.env.emplace_back("PYTHONPATH", pythonpath);
  }

  // 3. Ensure C++ Python bindings (_engine*.so) are built
  if (!root.empty() && fs::is_directory(root / "python")) {
    bool need_bindings_build = false;
    bool has_so = false;
    std::error_code ec;

    // Check python/paperboat/ for _engine*.so
    fs::path pkg_dir = root / "python" / "paperboat";
    std::filesystem::file_time_type so_time;
    if (fs::is_directory(pkg_dir)) {
      for (const auto& entry : fs::directory_iterator(pkg_dir, ec)) {
        auto fn = entry.path().filename().string();
        if (fn.starts_with("_engine") && fn.ends_with(".so")) {
          has_so = true;
          so_time = fs::last_write_time(entry.path(), ec);
          break;
        }
      }
    }

    if (!has_so) {
      need_bindings_build = true;
    } else {
      // Check if python/src/engine.cpp or core/toolkit headers are newer than .so
      for (const auto& entry : fs::recursive_directory_iterator(root / "python" / "src", ec)) {
        if (entry.is_regular_file() && fs::last_write_time(entry.path(), ec) > so_time) {
          need_bindings_build = true;
          break;
        }
      }
    }

    if (need_bindings_build) {
      fs::path py_build = root / "python" / "build";
      ProcessRunner::run_sync({"cmake", "-S", (root / "python").string(), "-B", py_build.string()});
      ProcessRunner::run_sync({"cmake", "--build", py_build.string()});
    }
  }

  return env;
}

BackendKind BackendManager::detect_backend_kind(const fs::path& path) {
  if (fs::is_regular_file(path)) {
    if (path.filename() == "server.py") return BackendKind::Python;
    if (path.filename() == "CMakeLists.txt") return BackendKind::Cpp;
  }
  if (fs::is_directory(path)) {
    if (fs::is_regular_file(path / "server.py")) return BackendKind::Python;
    if (fs::is_regular_file(path / "CMakeLists.txt")) return BackendKind::Cpp;

    const auto btoml = path / "backend.toml";
    if (fs::is_regular_file(btoml)) {
      try {
        auto tbl = toml::parse_file(btoml.string());
        std::string lang = tbl["backend"]["language"].value_or(tbl["language"].value_or(""));
        if (lang == "python") return BackendKind::Python;
        if (lang == "cpp") return BackendKind::Cpp;
        std::string eng = tbl["backend"]["engine"].value_or(tbl["engine"].value_or(""));
        if (eng.find("python") != std::string::npos) return BackendKind::Python;
        if (eng.find("cpp") != std::string::npos) return BackendKind::Cpp;
      } catch (...) {}
    }
  }
  return BackendKind::Unknown;
}

fs::path BackendManager::resolve_backend_dir(const fs::path& path_or_domain) {
  auto is_valid_backend_dir = [](const fs::path& p) {
    return fs::is_directory(p) && (
        fs::is_regular_file(p / "descriptor.toml") ||
        fs::is_regular_file(p / "backend.toml") ||
        fs::is_regular_file(p / "server.py") ||
        fs::is_regular_file(p / "CMakeLists.txt")
    );
  };

  if (path_or_domain.empty() || path_or_domain == ".") {
    return fs::current_path();
  }

  if (fs::exists(path_or_domain)) {
    if (is_valid_backend_dir(path_or_domain)) return path_or_domain;
    if (fs::is_regular_file(path_or_domain)) return path_or_domain.parent_path();
  }

  // Look in paperboat-examples or examples/backends
  const fs::path root = toolkit::find_repo_root();
  const fs::path edir = find_examples_dir();
  const std::string name = path_or_domain.string();
  std::vector<fs::path> candidates = {
      edir / name,
      edir / (name + "-cpp"),
      edir / (name + "-python"),
      root / "examples" / "backends" / name,
      root / "examples" / "backends" / (name + "-cpp"),
      root / "examples" / "backends" / (name + "-python"),
  };

  for (const auto& c : candidates) {
    if (is_valid_backend_dir(c)) return c;
  }

  return path_or_domain;
}

int BackendManager::run_backend(const fs::path& path_or_domain, const BackendRunOptions& opts) {
  const fs::path dir = resolve_backend_dir(path_or_domain);
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
  auto log_cb = [domain_name](std::string_view stream, std::string_view line) {
    if (stream == "stderr") {
      std::cerr << "[" << domain_name << "] " << line << '\n';
    } else {
      std::cout << "[" << domain_name << "] " << line << '\n';
    }
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
    fs::path bin;
    if (manifest.has_manifest && !manifest.run.empty() && fs::is_regular_file(dir / manifest.run)) {
      bin = dir / manifest.run;
    } else {
      std::error_code ec;
      for (const auto& entry : fs::directory_iterator(dir / "build", ec)) {
        if (entry.is_regular_file()) {
          auto fn = entry.path().filename().string();
          if (fn.starts_with("paperboat-") && !fn.ends_with(".o") && !fn.ends_with(".a")) {
            bin = entry.path();
            break;
          }
        }
      }
    }
    if (bin.empty()) {
      std::cerr << "error: compiled binary not found in " << (dir / "build") << '\n';
      return 1;
    }

    std::vector<std::string> args = {bin.string(), "--port", std::to_string(port)};
    for (const auto& [k, v] : opts.vars) {
      args.push_back("--var");
      args.push_back(k + "=" + v);
    }

    std::cout << "paperboat running " << bin.filename().string() << " on ws://localhost:" << port << '\n';
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
    for (const auto& [k, v] : opts.vars) {
      args.push_back("--var");
      args.push_back(k + "=" + v);
    }

    std::cout << "paperboat running python " << server_script.filename().string() << " on ws://localhost:" << port << '\n';
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

fs::path BackendManager::find_examples_dir(const fs::path& hint) {
  if (const char* env = std::getenv("PAPERBOAT_EXAMPLES_DIR"); env && *env && fs::is_directory(env)) {
    return env;
  }
  if (!hint.empty() && fs::is_directory(hint)) return hint;

  const fs::path root = toolkit::find_repo_root();
  auto is_examples_dir = [](const fs::path& p) {
    return fs::is_directory(p) && (
        fs::is_regular_file(p / "cart-pole-cpp" / "CMakeLists.txt") ||
        fs::is_regular_file(p / "mass-spring-python" / "server.py")
    );
  };

  if (!root.empty()) {
    if (is_examples_dir(root.parent_path() / "paperboat-examples")) {
      return root.parent_path() / "paperboat-examples";
    }
  }

  const fs::path fallback = "/home/cyyth/.local/share/opencode/worktree/f0d43b/paperboat-examples";
  if (is_examples_dir(fallback)) return fallback;

  if (!root.empty() && is_examples_dir(root / "examples" / "backends")) {
    return root / "examples" / "backends";
  }

  if (!root.empty() && fs::is_directory(root / "examples" / "backends")) {
    return root / "examples" / "backends";
  }

  return {};
}

std::vector<ExampleBackend> BackendManager::get_examples(const fs::path& examples_dir) {
  const fs::path edir = find_examples_dir(examples_dir);
  std::vector<ExampleBackend> list = {
      {8765, "mass-spring-python", "python", edir / "mass-spring-python", false, 0, "ws://localhost:8765"},
      {8766, "cart-pole-cpp", "cpp", edir / "cart-pole-cpp", false, 0, "ws://localhost:8766"},
      {8767, "parametric-python", "python", edir / "parametric-python", false, 0, "ws://localhost:8767"},
      {8768, "runner-python", "python", edir / "runner-python", false, 0, "ws://localhost:8768"},
      {8769, "animation-python", "python", edir / "animation-python", false, 0, "ws://localhost:8769"},
      {8770, "parts-cpp", "cpp", edir / "parts-cpp", false, 0, "ws://localhost:8770"},
      {8771, "chain-python", "python", edir / "chain-python", false, 0, "ws://localhost:8771"},
      {8772, "supply-python", "python", edir / "supply-python", false, 0, "ws://localhost:8772"},
      {8773, "double-pendulum-cpp", "cpp", edir / "double-pendulum-cpp", false, 0, "ws://localhost:8773"},
  };

  for (auto& item : list) {
    if (is_port_listening(item.port)) {
      item.running = true;
      item.pid = find_pid_by_port(item.port);
    }
  }
  return list;
}

bool BackendManager::start_examples(const fs::path& examples_dir) {
  const fs::path edir = find_examples_dir(examples_dir);
  if (edir.empty() || !fs::is_directory(edir)) {
    std::cerr << "error: paperboat-examples directory not found. Please set PAPERBOAT_EXAMPLES_DIR.\n";
    return false;
  }

  std::cout << "Using examples directory: " << edir.string() << '\n';
  stop_examples();

  const fs::path root = toolkit::find_repo_root();
  auto py_env = prepare_python_env(root);

  auto examples = get_examples(edir);
  for (const auto& ex : examples) {
    if (!fs::is_directory(ex.dir)) {
      std::cout << "[skip] " << ex.name << " (directory not found: " << ex.dir << ")\n";
      continue;
    }

    if (ex.type == "cpp") {
      std::cout << "==> Building " << ex.name << "…\n";
      std::string err;
      if (!ensure_built(ex.dir, false, false, &err)) {
        std::cerr << "warning: " << ex.name << " build failed: " << err << '\n';
        continue;
      }

      fs::path bin;
      std::error_code ec;
      for (const auto& entry : fs::directory_iterator(ex.dir / "build", ec)) {
        if (entry.is_regular_file()) {
          auto fn = entry.path().filename().string();
          if (fn.starts_with("paperboat-") && !fn.ends_with(".o") && !fn.ends_with(".a")) {
            bin = entry.path();
            break;
          }
        }
      }
      if (bin.empty()) {
        std::cerr << "warning: binary for " << ex.name << " not found\n";
        continue;
      }

      const fs::path log_path = fs::path("/tmp") / ("backend-" + std::to_string(ex.port) + ".log");
      int pid = ProcessRunner::run_detached({bin.string(), "--port", std::to_string(ex.port)}, root, {}, log_path);
      if (pid > 0) {
        std::cout << "Started " << ex.name << " on port " << ex.port << " (PID " << pid << ")\n";
        RunningBackend rb;
        rb.port = ex.port;
        rb.pid = pid;
        rb.engine = "paperboat/" + ex.name;
        rb.domain = ex.name.substr(0, ex.name.find("-cpp"));
        rb.cwd = root;
        rb.backend_dir = ex.dir;
        rb.executable = bin.string();
        rb.ws_url = "ws://localhost:" + std::to_string(ex.port);
        rb.started_at = current_timestamp();
        register_running_backend(rb);
      }
    } else {
      // Python
      const fs::path log_path = fs::path("/tmp") / ("backend-" + std::to_string(ex.port) + ".log");
      int pid = ProcessRunner::run_detached({py_env.python_bin.string(), "server.py", "--port", std::to_string(ex.port)},
                                            ex.dir, py_env.env, log_path);
      if (pid > 0) {
        std::cout << "Started " << ex.name << " on port " << ex.port << " (PID " << pid << ")\n";
        RunningBackend rb;
        rb.port = ex.port;
        rb.pid = pid;
        rb.engine = "python";
        rb.domain = ex.name.substr(0, ex.name.find("-python"));
        rb.cwd = ex.dir;
        rb.backend_dir = ex.dir;
        rb.executable = py_env.python_bin.string() + " server.py";
        rb.ws_url = "ws://localhost:" + std::to_string(ex.port);
        rb.started_at = current_timestamp();
        register_running_backend(rb);
      }
    }
  }

  std::cout << "==> All example backends launched (ports 8765-8773).\n";
  return true;
}

bool BackendManager::stop_examples() {
  std::cout << "==> Stopping all example backends (ports 8765-8773)…\n";
  for (int p = 8765; p <= 8773; ++p) {
    stop_user_service("paperboat-backend-" + std::to_string(p) + ".service");
    free_port(p, 1000);
    unregister_running_backend(p);
  }
  std::cout << "==> All example backends stopped.\n";
  return true;
}

std::vector<ExampleBackend> BackendManager::status_examples(const fs::path& examples_dir) {
  return get_examples(examples_dir);
}

bool BackendManager::start_app(int port, const std::string& host, bool daemon) {
  const fs::path root = toolkit::find_repo_root();
  if (root.empty()) {
    std::cerr << "error: repository root not found\n";
    return false;
  }

  free_port(port, 2000);
  stop_user_service("paperboat-web.service");

  std::cout << "==> Starting web application in dev mode on port " << port << "…\n";
  std::vector<std::string> args = {"npm", "run", "dev", "--", "--host", host, "--port", std::to_string(port)};

  if (daemon) {
    fs::path log_path = "/tmp/paperboat-web.log";
    int pid = ProcessRunner::run_detached(args, root, {}, log_path);
    if (pid > 0) {
      std::cout << "Web application started in background (PID " << pid << ", log: " << log_path << ")\n";
      return true;
    }
    return false;
  } else {
    return ProcessRunner::run_stream(args, root, {}, [](std::string_view stream, std::string_view line) {
      if (stream == "stderr") std::cerr << line << '\n';
      else std::cout << line << '\n';
    }) == 0;
  }
}

bool BackendManager::stop_app(int port) {
  std::cout << "==> Stopping web application on port " << port << "…\n";
  stop_user_service("paperboat-web.service");
  free_port(port, 2000);
  std::cout << "==> Web application stopped.\n";
  return true;
}

bool BackendManager::restart_app(int port, const std::string& host) {
  stop_app(port);
  return start_app(port, host, false);
}

bool BackendManager::build_app() {
  const fs::path root = toolkit::find_repo_root();
  if (root.empty()) {
    std::cerr << "error: repository root not found\n";
    return false;
  }

  std::cout << "==> Building web application…\n";
  auto res = ProcessRunner::run_sync({"npm", "run", "build"}, root);
  if (!res.ok()) {
    std::cerr << res.stderr_text << '\n';
    return false;
  }
  std::cout << res.stdout_text << '\n';
  std::cout << "==> Web build complete.\n";
  return true;
}

BackendManifest BackendManager::load_manifest(const fs::path& backend_dir, std::string* error_msg) {
  BackendManifest m;
  const fs::path manifest_path = backend_dir / "backend.toml";
  if (!fs::is_regular_file(manifest_path)) {
    if (error_msg) *error_msg = "backend.toml missing in " + backend_dir.string();
    return m;
  }

  try {
    auto doc = toml::parse_file(manifest_path.string());
    m.has_manifest = true;

    auto backend_tbl = doc["backend"].as_table();
    if (!backend_tbl) {
      if (error_msg) *error_msg = "missing [backend] table in " + manifest_path.string();
      return m;
    }

    if (!doc["backend"]["format"].is_integer()) {
      if (error_msg) *error_msg = "missing or invalid [backend].format in " + manifest_path.string();
      return m;
    }
    m.format = static_cast<int>(doc["backend"]["format"].value_or(-1));
    if (m.format != 0) {
      if (error_msg) *error_msg = "unsupported backend.toml format version " + std::to_string(m.format) + " (expected 0)";
      return m;
    }

    m.name = doc["backend"]["name"].value_or(std::string{});
    m.version = doc["backend"]["version"].value_or("0.1.0");
    m.engine = doc["backend"]["engine"].value_or(">=0.1");
    m.language = doc["backend"]["language"].value_or(std::string{});
    m.run = doc["backend"]["run"].value_or(std::string{});
    m.port = static_cast<int>(doc["backend"]["port"].value_or(8765));
    m.descriptor = doc["backend"]["descriptor"].value_or("descriptor.toml");

    if (auto tks = doc["backend"]["toolkits"].as_array()) {
      for (const auto& el : *tks) {
        if (auto s = el.value<std::string>()) {
          m.toolkits.push_back(*s);
        }
      }
    }

    if (doc["test"].is_table()) {
      m.golden = doc["test"]["golden"].value_or(std::string{});
    }

    if (m.name.empty()) {
      if (error_msg) *error_msg = "backend.toml: missing required field 'name'";
      return m;
    }
    if (m.language != "cpp" && m.language != "python") {
      if (error_msg) *error_msg = "backend.toml: invalid or missing 'language' (must be 'cpp' or 'python')";
      return m;
    }
    if (m.run.empty()) {
      if (error_msg) *error_msg = "backend.toml: missing required field 'run'";
      return m;
    }
    if (m.descriptor.empty()) {
      if (error_msg) *error_msg = "backend.toml: 'descriptor' cannot be empty";
      return m;
    }
  } catch (const toml::parse_error& err) {
    if (error_msg) {
      *error_msg = "backend.toml parse error at line " + std::to_string(err.source().begin.line) +
                   ":" + std::to_string(err.source().begin.column) + ": " + std::string(err.description());
    }
    return m;
  } catch (const std::exception& err) {
    if (error_msg) *error_msg = err.what();
    return m;
  }

  return m;
}

int BackendManager::find_free_port(int start_port, int end_port) {
  for (int p = start_port; p <= end_port; ++p) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) continue;
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(p));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      close(sock);
      if (!is_port_listening(p)) {
        return p;
      }
    } else {
      close(sock);
    }
  }
  return 0;
}

CheckResult BackendManager::check_backend(const fs::path& path, bool fast) {
  CheckResult res;
  const fs::path dir = resolve_backend_dir(path);
  if (!fs::is_directory(dir)) {
    res.errors.push_back("backend directory not found: " + path.string());
    return res;
  }

  const fs::path repo_root = toolkit::find_repo_root(dir);

  // -------------------------------------------------------------
  // Stage 1: Manifest & Folder Layout
  // -------------------------------------------------------------
  CheckStep step_manifest{"manifest", false, false, "", 0.0};
  auto t_start = std::chrono::steady_clock::now();

  std::string manifest_err;
  BackendManifest manifest = load_manifest(dir, &manifest_err);
  if (!manifest_err.empty() || !manifest.has_manifest) {
    step_manifest.message = manifest_err.empty() ? "failed to load backend.toml" : manifest_err;
    step_manifest.ok = false;
    res.errors.push_back(step_manifest.message);
    res.steps.push_back(step_manifest);
    return res;
  }

  res.name = manifest.name;
  res.version = manifest.version;
  res.language = manifest.language;
  res.engine = manifest.engine;

  // Check descriptor file exists
  const fs::path desc_file = dir / manifest.descriptor;
  if (!fs::is_regular_file(desc_file)) {
    step_manifest.message = "descriptor file missing: " + manifest.descriptor;
    step_manifest.ok = false;
    res.errors.push_back(step_manifest.message);
    res.steps.push_back(step_manifest);
    return res;
  }

  // Check build / entry point files
  if (manifest.language == "cpp") {
    if (!fs::is_regular_file(dir / "CMakeLists.txt")) {
      step_manifest.message = "CMakeLists.txt missing in C++ backend: " + dir.string();
      step_manifest.ok = false;
      res.errors.push_back(step_manifest.message);
      res.steps.push_back(step_manifest);
      return res;
    }
  } else if (manifest.language == "python") {
    if (!fs::is_regular_file(dir / manifest.run) && !fs::is_regular_file(dir / "server.py")) {
      step_manifest.message = "Python entrypoint missing (" + manifest.run + " or server.py) in " + dir.string();
      step_manifest.ok = false;
      res.errors.push_back(step_manifest.message);
      res.steps.push_back(step_manifest);
      return res;
    }
  }

  // Check golden file if declared
  if (!manifest.golden.empty() && !fs::is_regular_file(dir / manifest.golden)) {
    step_manifest.message = "declared golden file missing: " + (dir / manifest.golden).string();
    step_manifest.ok = false;
    res.errors.push_back(step_manifest.message);
    res.steps.push_back(step_manifest);
    return res;
  }

  // Check required toolkits
  std::vector<std::string> all_toolkits = manifest.toolkits;
  const auto known = toolkit::list_toolkits(repo_root);
  const auto reqs = toolkit::load_toolkit_requirements(dir, known);
  for (const auto& r : reqs) {
    if (std::find(all_toolkits.begin(), all_toolkits.end(), r) == all_toolkits.end()) {
      all_toolkits.push_back(r);
    }
  }
  for (const auto& tk : all_toolkits) {
    const fs::path tk_dir = repo_root / "engine" / "toolkit" / tk;
    if (!fs::is_regular_file(tk_dir / "toolkit.toml")) {
      res.missing_toolkits.push_back(tk);
      res.warnings.push_back("missing required toolkit: " + tk);
    }
  }
  if (!res.missing_toolkits.empty()) {
    step_manifest.message = "missing required toolkit: " + res.missing_toolkits.front();
    step_manifest.ok = false;
    res.errors.push_back(step_manifest.message);
    res.steps.push_back(step_manifest);
    return res;
  }

  // Parse descriptor syntax
  try {
    auto doc = toml::parse_file(desc_file.string());
    res.domain = doc["domain"].value_or(manifest.name);
  } catch (const toml::parse_error& err) {
    step_manifest.message = "descriptor syntax error: " + desc_file.string() + ":" +
                            std::to_string(err.source().begin.line) + ":" +
                            std::to_string(err.source().begin.column) + ": " + std::string(err.description());
    step_manifest.ok = false;
    res.errors.push_back(step_manifest.message);
    res.steps.push_back(step_manifest);
    return res;
  }

  auto t_end = std::chrono::steady_clock::now();
  step_manifest.duration_seconds = std::chrono::duration<double>(t_end - t_start).count();
  step_manifest.ok = true;
  step_manifest.message = "format 0, " + manifest.descriptor + ", " + manifest.language;
  res.steps.push_back(step_manifest);
  res.manifest_ok = true;

  // -------------------------------------------------------------
  // Stage 2: Catalog & Block Completeness
  // -------------------------------------------------------------
  CheckStep step_catalog{"catalog", false, false, "", 0.0};
  t_start = std::chrono::steady_clock::now();

  bool catalog_success = false;
  try {
    const auto catalog = toolkit::load_descriptor_file(desc_file);
    res.domain = catalog.domain.empty() ? res.domain : catalog.domain;
    res.block_count = static_cast<int>(catalog.entries.size());
    res.object_count = static_cast<int>(catalog.objects.size());
    res.has_starter_graph = !catalog.graph.empty();
    res.has_starter_station = !catalog.station.empty();
    catalog_success = true;
  } catch (const toolkit::DescriptorError& err) {
    std::string msg = err.what();
    if (msg.find("no block type") != std::string::npos || msg.find("no station template") != std::string::npos) {
      // Check if the missing type is a domain block provided by backend code
      std::string type_name;
      auto q1 = msg.find('"');
      if (q1 != std::string::npos) {
        auto q2 = msg.find('"', q1 + 1);
        if (q2 != std::string::npos) {
          type_name = msg.substr(q1 + 1, q2 - q1 - 1);
        }
      }

      bool is_domain_block = false;
      if (!type_name.empty()) {
        if (type_name == res.domain || type_name.starts_with(res.domain + "-") || type_name.starts_with(res.domain + "_")) {
          is_domain_block = true;
        } else {
          // Scan domain source files for type_name
          std::error_code ec;
          for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file()) {
              auto ext = entry.path().extension().string();
              if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".py") {
                std::ifstream ifs(entry.path());
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                if (content.find("\"" + type_name + "\"") != std::string::npos ||
                    content.find("'" + type_name + "'") != std::string::npos) {
                  is_domain_block = true;
                  break;
                }
              }
            }
          }
        }
      }

      if (is_domain_block) {
        res.warnings.push_back("domain block/station template provided by runtime: " + msg);
        catalog_success = true;
      } else {
        step_catalog.message = msg;
        res.errors.push_back(msg);
      }
    } else {
      step_catalog.message = msg;
      res.errors.push_back(msg);
    }
  } catch (const std::exception& err) {
    step_catalog.message = err.what();
    res.errors.push_back(err.what());
  }

  t_end = std::chrono::steady_clock::now();
  step_catalog.duration_seconds = std::chrono::duration<double>(t_end - t_start).count();

  if (!catalog_success) {
    step_catalog.ok = false;
    res.steps.push_back(step_catalog);
    return res;
  }

  step_catalog.ok = true;
  step_catalog.message = std::to_string(res.block_count) + " blocks, " +
                         std::to_string(res.object_count) + " station objects";
  res.steps.push_back(step_catalog);
  res.catalog_ok = true;

  // -------------------------------------------------------------
  // If fast mode, skip stages 3 & 4
  // -------------------------------------------------------------
  if (fast) {
    CheckStep step_smoke{"smoke", false, true, "skipped (--fast)", 0.0};
    res.steps.push_back(step_smoke);
    CheckStep step_golden{"golden", false, true, "skipped (--fast)", 0.0};
    res.steps.push_back(step_golden);
    res.ok = res.errors.empty();
    return res;
  }

  // -------------------------------------------------------------
  // Stage 3: Live Protocol Smoke Test
  // -------------------------------------------------------------
  CheckStep step_smoke{"smoke", false, false, "", 0.0};
  t_start = std::chrono::steady_clock::now();

  auto py_env = prepare_python_env(repo_root, dir);
  if (manifest.language == "cpp") {
    std::string build_err;
    if (!ensure_built(dir, false, false, &build_err)) {
      step_smoke.message = "build failed: " + build_err;
      step_smoke.ok = false;
      res.errors.push_back(step_smoke.message);
      res.steps.push_back(step_smoke);
      return res;
    }
  }

  int smoke_port = find_free_port(18765, 18999);
  if (smoke_port <= 0) {
    step_smoke.message = "no ephemeral port available for smoke test";
    step_smoke.ok = false;
    res.errors.push_back(step_smoke.message);
    res.steps.push_back(step_smoke);
    return res;
  }

  // Construct launch command
  std::vector<std::string> launch_cmd;
  if (manifest.language == "cpp") {
    fs::path bin = dir / manifest.run;
    if (!fs::is_regular_file(bin)) {
      step_smoke.message = "executable not found: " + bin.string();
      step_smoke.ok = false;
      res.errors.push_back(step_smoke.message);
      res.steps.push_back(step_smoke);
      return res;
    }
    launch_cmd = {bin.string(), "--port", std::to_string(smoke_port)};
  } else {
    fs::path script = dir / manifest.run;
    launch_cmd = {py_env.python_bin.string(), script.string(), "--port", std::to_string(smoke_port)};
  }

  fs::path log_path = runtime_dir() / ("smoke-" + std::to_string(smoke_port) + ".log");
  fs::create_directories(runtime_dir());

  int backend_pid = ProcessRunner::run_detached(launch_cmd, dir, py_env.env, log_path);
  if (backend_pid <= 0) {
    step_smoke.message = "failed to start backend process";
    step_smoke.ok = false;
    res.errors.push_back(step_smoke.message);
    res.steps.push_back(step_smoke);
    return res;
  }

  struct BackendCleaner {
    int pid;
    int port;
    ~BackendCleaner() {
      if (pid > 0) {
        kill(-pid, SIGTERM);
        kill(pid, SIGTERM);
        BackendManager::free_port(port, 1000);
        BackendManager::unregister_running_backend(port);
      }
    }
  } cleaner{backend_pid, smoke_port};

  RunningBackend rb;
  rb.port = smoke_port;
  rb.pid = backend_pid;
  rb.domain = res.domain;
  rb.backend_dir = dir;
  rb.started_at = current_timestamp();
  register_running_backend(rb);

  bool listening = false;
  for (int i = 0; i < 50; ++i) {
    if (is_port_listening(smoke_port)) {
      listening = true;
      break;
    }
    if (!ProcessRunner::is_alive(backend_pid)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (!listening) {
    std::string err_output;
    std::ifstream log_file(log_path);
    if (log_file) {
      std::stringstream buf;
      buf << log_file.rdbuf();
      err_output = buf.str();
    }
    step_smoke.message = "backend failed to start on port " + std::to_string(smoke_port) +
                         (err_output.empty() ? "" : ": " + err_output);
    step_smoke.ok = false;
    res.errors.push_back(step_smoke.message);
    res.steps.push_back(step_smoke);
    return res;
  }

  fs::path smoke_script = repo_root / "protocol" / "smoke_test.py";
  auto smoke_res = ProcessRunner::run_sync(
      {py_env.python_bin.string(), smoke_script.string(), "--port", std::to_string(smoke_port)},
      repo_root, py_env.env, std::chrono::milliseconds(20000));

  t_end = std::chrono::steady_clock::now();
  step_smoke.duration_seconds = std::chrono::duration<double>(t_end - t_start).count();

  if (!smoke_res.ok()) {
    std::string failure_msg = !smoke_res.stdout_text.empty() ? smoke_res.stdout_text : smoke_res.stderr_text;
    if (failure_msg.empty()) failure_msg = "smoke test failed (exit code " + std::to_string(smoke_res.exit_code) + ")";
    step_smoke.message = failure_msg;
    step_smoke.ok = false;
    res.errors.push_back(failure_msg);
    res.steps.push_back(step_smoke);
    return res;
  }

  step_smoke.ok = true;
  step_smoke.message = "protocol smoke test passed on port " + std::to_string(smoke_port);
  res.steps.push_back(step_smoke);
  res.smoke_ok = true;

  // -------------------------------------------------------------
  // Stage 4: Golden Headless Comparison
  // -------------------------------------------------------------
  CheckStep step_golden{"golden", false, false, "", 0.0};
  t_start = std::chrono::steady_clock::now();

  if (manifest.golden.empty()) {
    step_golden.skipped = true;
    step_golden.ok = true;
    step_golden.message = "no golden test declared";
    res.steps.push_back(step_golden);
    res.golden_ok = true;
  } else {
    fs::path golden_script = repo_root / "protocol" / "golden.py";
    auto golden_res = ProcessRunner::run_sync(
        {py_env.python_bin.string(), golden_script.string(), dir.string()},
        repo_root, py_env.env, std::chrono::milliseconds(30000));

    t_end = std::chrono::steady_clock::now();
    step_golden.duration_seconds = std::chrono::duration<double>(t_end - t_start).count();

    if (!golden_res.ok()) {
      std::string failure_msg = !golden_res.stdout_text.empty() ? golden_res.stdout_text : golden_res.stderr_text;
      if (failure_msg.empty()) failure_msg = "golden test failed (exit code " + std::to_string(golden_res.exit_code) + ")";
      step_golden.message = failure_msg;
      step_golden.ok = false;
      res.errors.push_back(failure_msg);
      res.steps.push_back(step_golden);
      return res;
    }

    step_golden.ok = true;
    step_golden.message = manifest.golden + " matched";
    res.steps.push_back(step_golden);
    res.golden_ok = true;
  }

  res.ok = res.errors.empty();
  return res;
}

PackResult BackendManager::pack_backend(const fs::path& path, const fs::path& out_path, bool /*fast*/) {
  PackResult res;
  const fs::path dir = resolve_backend_dir(path);
  if (!fs::is_directory(dir)) {
    res.errors.push_back("directory not found: " + path.string());
    return res;
  }

  // Run fast check first
  auto check_res = check_backend(dir, /*fast=*/true);
  if (!check_res.ok) {
    res.errors.push_back("cannot pack: backend verification failed");
    for (const auto& err : check_res.errors) res.errors.push_back("  " + err);
    return res;
  }

  BackendManifest manifest = load_manifest(dir);
  res.name = manifest.name.empty() ? dir.filename().string() : manifest.name;
  res.version = manifest.version.empty() ? "0.1.0" : manifest.version;

  fs::path package_out = out_path;
  if (package_out.empty()) {
    package_out = fs::current_path() / (res.name + "-" + res.version + ".paperboat-pkg");
  }

  const fs::path repo_root = toolkit::find_repo_root(dir);
  auto py_env = prepare_python_env(repo_root, dir);
  fs::path script = repo_root / "scripts" / "package_backend.py";

  auto pack_proc = ProcessRunner::run_sync(
      {py_env.python_bin.string(), script.string(), "pack", "--dir", dir.string(), "--out", package_out.string()},
      repo_root, py_env.env, std::chrono::milliseconds(30000));

  if (!pack_proc.ok()) {
    res.errors.push_back("packing script failed: " +
                         (!pack_proc.stdout_text.empty() ? pack_proc.stdout_text : pack_proc.stderr_text));
    return res;
  }

  try {
    auto j = json::parse(pack_proc.stdout_text);
    if (!j.value("ok", false)) {
      res.errors.push_back(j.value("error", "unknown packing error"));
      return res;
    }
    res.ok = true;
    res.package_path = package_out;
    res.name = j.value("name", res.name);
    res.version = j.value("version", res.version);
    res.file_count = j.value("file_count", 0);
    res.total_bytes = j.value("total_bytes", static_cast<uint64_t>(0));
    res.sha256 = j.value("sha256", std::string{});
  } catch (const std::exception& e) {
    res.errors.push_back("failed to parse pack output: " + std::string(e.what()));
    return res;
  }

  return res;
}

UnpackResult BackendManager::unpack_backend(const fs::path& archive_path, const fs::path& dest_dir, bool verify) {
  UnpackResult res;
  if (!fs::is_regular_file(archive_path)) {
    res.errors.push_back("package archive not found: " + archive_path.string());
    return res;
  }

  const fs::path repo_root = toolkit::find_repo_root();
  auto py_env = prepare_python_env(repo_root);
  fs::path script = repo_root / "scripts" / "package_backend.py";

  fs::path target_dir = dest_dir;
  if (target_dir.empty()) {
    std::string stem = archive_path.stem().string();
    target_dir = fs::current_path() / stem;
  }

  std::vector<std::string> args = {
      py_env.python_bin.string(), script.string(), "unpack",
      "--pkg", archive_path.string(), "--dir", target_dir.string()
  };
  if (verify) args.push_back("--verify");

  auto unpack_proc = ProcessRunner::run_sync(args, repo_root, py_env.env, std::chrono::milliseconds(30000));
  if (!unpack_proc.ok()) {
    res.errors.push_back("unpack failed: " +
                         (!unpack_proc.stdout_text.empty() ? unpack_proc.stdout_text : unpack_proc.stderr_text));
    return res;
  }

  try {
    auto j = json::parse(unpack_proc.stdout_text);
    if (!j.value("ok", false)) {
      res.errors.push_back(j.value("error", "unknown unpack error"));
      return res;
    }
    res.ok = true;
    res.destination_dir = target_dir;
    res.name = j.value("name", "");
    res.version = j.value("version", "");
    res.file_count = j.value("file_count", 0);
  } catch (const std::exception& e) {
    res.errors.push_back("failed to parse unpack output: " + std::string(e.what()));
    return res;
  }

  if (verify) {
    auto check_res = check_backend(target_dir, /*fast=*/true);
    if (!check_res.ok) {
      res.errors.push_back("post-unpack verification failed:");
      for (const auto& err : check_res.errors) res.errors.push_back("  " + err);
      res.ok = false;
      return res;
    }
  }

  return res;
}

}  // namespace paperboat::core
