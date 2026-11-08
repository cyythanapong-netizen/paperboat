#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

namespace {

// The installed SDK next to this binary: <prefix>/bin/paperboat, <prefix>/lib*/cmake/Paperboat (pb#177).
fs::path sdk_dir() {
  const fs::path exe = platform::exe_path();
  if (exe.empty()) return {};
  const fs::path prefix = exe.parent_path().parent_path();
  std::error_code ec;
  for (const char* lib : {"lib", "lib64"}) {
    if (const fs::path dir = prefix / lib / "cmake" / "Paperboat"; fs::is_regular_file(dir / "PaperboatConfig.cmake", ec)) return dir;
  }
  // Debian puts libraries in lib/<multiarch>.
  for (const auto& entry : fs::directory_iterator(prefix / "lib", ec)) {
    if (const fs::path dir = entry.path() / "cmake" / "Paperboat"; fs::is_regular_file(dir / "PaperboatConfig.cmake", ec)) return dir;
  }
  return {};
}

}  // namespace

bool BackendManager::needs_rebuild(const fs::path& backend_dir, const fs::path& build_dir) {
  const fs::path bdir = build_dir.empty() ? (backend_dir / "build") : build_dir;
  const fs::path stamp_file = bdir / ".paperboat-build.stamp";

  if (!fs::exists(bdir / "CMakeCache.txt") || !fs::is_regular_file(stamp_file)) {
    return true;
  }

  const fs::path bin = built_binary(bdir);
  if (bin.empty()) return true;
  std::error_code ec;
  const auto bin_time = fs::last_write_time(bin, ec);

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
    if (verbose) std::cout << "[build] up to date (skipped)" << std::endl;
    if (elapsed_seconds) *elapsed_seconds = seconds_since(start_time);
    return true;
  }

  if (!fs::exists(build_dir / "CMakeCache.txt")) {
    if (verbose) std::cout << "[build] configuring with cmake…" << std::endl;
    const fs::path root = toolkit::find_repo_root(backend_dir);
    std::vector<std::string> config_cmd = {"cmake", "-S", backend_dir.string(), "-B", build_dir.string()};
    if (!root.empty() && fs::is_regular_file(root / "engine" / "CMakeLists.txt")) {
      config_cmd.push_back("-DPAPERBOAT_ENGINE_DIR=" + (root / "engine").string());
    } else if (const fs::path sdk = sdk_dir(); !sdk.empty()) {
      // An installed package: build against its SDK (pb#177), which is a Release build.
      config_cmd.push_back("-DPaperboat_DIR=" + sdk.string());
      config_cmd.push_back("-DCMAKE_BUILD_TYPE=Release");
    }
    auto config_res = ProcessRunner::run_sync(config_cmd);
    if (!config_res.ok()) {
      if (error_msg) *error_msg = "cmake configure failed: " + config_res.stderr_text;
      return false;
    }
  }

  if (verbose) std::cout << "[build] compiling backend…" << std::endl;
  auto build_res = ProcessRunner::run_sync({"cmake", "--build", build_dir.string(), "--config", "Release"});
  if (!build_res.ok()) {
    if (error_msg) *error_msg = "cmake build failed: " + build_res.stderr_text;
    return false;
  }

  // Update stamp file
  {
    std::ofstream stamp(build_dir / ".paperboat-build.stamp");
    stamp << current_timestamp() << '\n';
  }

  if (elapsed_seconds) *elapsed_seconds = seconds_since(start_time);
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

  env.env.emplace_back("no_proxy", "localhost,127.0.0.1");
  env.env.emplace_back("NO_PROXY", "localhost,127.0.0.1");
  env.env.emplace_back("PYTHONUNBUFFERED", "1");  // log lines reach `paperboat run` as they are printed

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

fs::path BackendManager::user_backends_dir() {
  if (const fs::path home = platform::home_dir(); !home.empty()) {
    return home / ".paperboat" / "backends";
  }
  return fs::temp_directory_path() / "paperboat" / "backends";
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

  const fs::path p_check = fix_pkk_typo(path_or_domain);

  // Absolute: callers run tools from other folders (the packer runs in the repo root).
  if (fs::exists(p_check)) {
    if (is_valid_backend_dir(p_check) || fs::is_regular_file(p_check)) return fs::absolute(p_check);
  }

  // Look in the extra search folders (the CLI adds the examples), user backends, or a sibling directory
  const std::string name = p_check.string();
  const fs::path ub_dir = user_backends_dir();

  std::vector<fs::path> candidates;
  for (const auto& d : search_dirs) {
    for (const auto& n : {name, name + "-cpp", name + "-python"}) candidates.push_back(d / n);
  }
  candidates.push_back(ub_dir / name);
  candidates.push_back(fs::current_path().parent_path() / name);

  for (const auto& c : candidates) {
    if (is_valid_backend_dir(c)) return c;
  }

  // Check matching directory in user_backends_dir with version prefix (e.g. name-0.1.0)
  std::error_code ec;
  if (fs::is_directory(ub_dir, ec)) {
    for (const auto& entry : fs::directory_iterator(ub_dir, ec)) {
      if (entry.is_directory() && entry.path().filename().string().starts_with(name + "-")) {
        if (is_valid_backend_dir(entry.path())) return entry.path();
      }
    }
  }

  return p_check;
}

}  // namespace paperboat::core
