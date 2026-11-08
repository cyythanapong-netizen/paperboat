#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

namespace {

void add_unique(std::vector<std::string>& list, const std::string& item) {
  if (std::ranges::find(list, item) == list.end()) list.push_back(item);
}

}  // namespace

BackendManifest BackendManager::load_manifest(const fs::path& backend_dir, std::string* error_msg) {
  BackendManifest m;
  const fs::path manifest_path = backend_dir / "backend.toml";
  if (!fs::is_regular_file(manifest_path)) {
    if (error_msg) *error_msg = "backend.toml missing in " + backend_dir.string();
    return m;
  }

  try {
    auto doc = toml::parse_file(manifest_path.string());

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
#ifdef _WIN32
    // A C++ backend's run names the program without .exe, the same on every platform.
    if (m.language == "cpp" && !m.run.empty() && fs::path(m.run).extension().empty()) m.run += ".exe";
#endif
    m.port = static_cast<int>(doc["backend"]["port"].value_or(8765));
    m.descriptor = doc["backend"]["descriptor"].value_or("descriptor.toml");

    auto parse_toolkit_node = [&](const toml::node* node) {
      if (!node) return;
      if (const auto* arr = node->as_array()) {
        for (const auto& el : *arr) {
          if (auto s = el.value<std::string>()) add_unique(m.toolkits, *s);
        }
      } else if (auto s = node->value<std::string>()) {
        add_unique(m.toolkits, *s);
      }
    };
    parse_toolkit_node(doc["backend"]["toolkits"].node());
    parse_toolkit_node(doc["backend"]["toolkit"].node());

    if (doc["test"].is_table()) {
      m.golden = doc["test"]["golden"].value_or(std::string{});
    }

    if (doc["backend"]["solver"].is_string()) {
      m.solver = doc["backend"]["solver"].value<std::string>().value();
    } else if (doc["run"]["solver"].is_string()) {
      m.solver = doc["run"]["solver"].value<std::string>().value();
    }

    if (!m.solver.empty()) {
      const auto& valid_solvers = toolkit::solve::names();
      if (std::find(valid_solvers.begin(), valid_solvers.end(), m.solver) == valid_solvers.end()) {
        std::string list;
        for (const auto& s : valid_solvers) list += (list.empty() ? "" : ", ") + s;
        if (error_msg) *error_msg = "backend.toml: invalid solver '" + m.solver + "' (expected one of: " + list + ")";
        return m;
      }
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

    m.has_manifest = true;
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
  net_init();
  for (int p = start_port; p <= end_port; ++p) {
    sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kNoSock) continue;
#ifndef _WIN32  // on Windows SO_REUSEADDR would let bind succeed on a port that is in use
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(p));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      close_sock(sock);
      if (!is_port_listening(p)) {
        return p;
      }
    } else {
      close_sock(sock);
    }
  }
  return 0;
}

CheckResult BackendManager::check_backend(const fs::path& path, bool fast, const CheckProgress& progress) {
  CheckResult res;
  fs::path target = fix_pkk_typo(path.empty() ? fs::current_path() : path);

  // If path is an archive (.ppk or .paperboat-pkg or .pkk), unpack to a temporary directory for verification
  fs::path temp_unpack_dir;
  if (is_package(target)) {
    temp_unpack_dir = fs::temp_directory_path() / ("pb_check_" + target.stem().string());
    std::error_code ec;
    fs::remove_all(temp_unpack_dir, ec);
    auto u_res = unpack_backend(target, temp_unpack_dir, /*verify=*/true);
    if (!u_res.ok) {
      res.errors.push_back("failed to unpack archive for verification");
      return res;
    }
    target = temp_unpack_dir;
  }

  struct TempCleaner {
    fs::path dir;
    ~TempCleaner() {
      if (!dir.empty()) {
        std::error_code ec;
        fs::remove_all(dir, ec);
      }
    }
  } temp_cleaner{temp_unpack_dir};

  const fs::path dir = resolve_backend_dir(target);
  if (!fs::is_directory(dir)) {
    res.errors.push_back("backend directory not found: " + path.string());
    return res;
  }

  const fs::path repo_root = toolkit::find_repo_root(dir);

  // Each stage reports when it starts and when it ends (`paperboat check --json-stream`).
  const auto begin = [&progress](const CheckStep& step) {
    if (progress) progress(step, false);
  };
  const auto finish = [&res, &progress](const CheckStep& step) {
    res.steps.push_back(step);
    if (progress) progress(step, true);
  };

  // A stage that fails ends the check: it is the last step, its message the last error.
  const auto fail = [&res, &finish](CheckStep& step, std::string message) {
    step.message = std::move(message);
    step.ok = false;
    res.errors.push_back(step.message);
    finish(step);
    return res;
  };

  // -------------------------------------------------------------
  // Stage 1: Manifest & Folder Layout
  // -------------------------------------------------------------
  CheckStep step_manifest{"manifest", false, false, "", 0.0};
  begin(step_manifest);
  auto t_start = std::chrono::steady_clock::now();

  std::string manifest_err;
  BackendManifest manifest = load_manifest(dir, &manifest_err);
  if (!manifest_err.empty() || !manifest.has_manifest) {
    return fail(step_manifest, manifest_err.empty() ? "failed to load backend.toml" : manifest_err);
  }

  res.name = manifest.name;
  res.version = manifest.version;
  res.language = manifest.language;
  res.engine = manifest.engine;
  res.solver = manifest.solver;

  // Check descriptor file exists
  const fs::path desc_file = dir / manifest.descriptor;
  if (!fs::is_regular_file(desc_file)) {
    return fail(step_manifest, "descriptor file missing: " + manifest.descriptor);
  }

  // Check build / entry point files
  if (manifest.language == "cpp") {
    if (!fs::is_regular_file(dir / "CMakeLists.txt")) {
      return fail(step_manifest, "CMakeLists.txt missing in C++ backend: " + dir.string());
    }
  } else if (manifest.language == "python") {
    if (!fs::is_regular_file(dir / manifest.run) && !fs::is_regular_file(dir / "server.py")) {
      return fail(step_manifest, "Python entrypoint missing (" + manifest.run + " or server.py) in " + dir.string());
    }
  }

  // Check golden file if declared
  if (!manifest.golden.empty() && !fs::is_regular_file(dir / manifest.golden)) {
    return fail(step_manifest, "declared golden file missing: " + (dir / manifest.golden).string());
  }

  // Check required toolkits
  std::vector<std::string> required_toolkits;
  for (const auto& tk : manifest.toolkits) {
    if (tk != "__all__") add_unique(required_toolkits, tk);
  }
  const auto known = toolkit::list_toolkits(repo_root);
  for (const auto& r : toolkit::load_toolkit_requirements(dir, known)) add_unique(required_toolkits, r);
  for (const auto& tk : required_toolkits) {
    const fs::path tk_dir = repo_root / "engine" / "toolkit" / tk;
    if (!fs::is_regular_file(tk_dir / "toolkit.toml")) {
      res.missing_toolkits.push_back(tk);
      res.warnings.push_back("missing required toolkit: " + tk);
    }
  }
  if (!res.missing_toolkits.empty()) {
    return fail(step_manifest, "missing required toolkit: " + res.missing_toolkits.front());
  }

  // Parse descriptor syntax
  bool has_all = std::find(manifest.toolkits.begin(), manifest.toolkits.end(), "__all__") != manifest.toolkits.end();
  try {
    auto doc = toml::parse_file(desc_file.string());
    res.domain = doc["domain"].value_or(manifest.name);
    for (const char* key : {"toolkits", "toolkit", "import"}) {
      if (auto arr = doc[key].as_array()) {
        for (const auto& el : *arr) {
          if (auto s = el.value<std::string>(); s && *s == "__all__") has_all = true;
        }
      } else if (auto s = doc[key].value<std::string>(); s && *s == "__all__") {
        has_all = true;
      }
    }
  } catch (const toml::parse_error& err) {
    return fail(step_manifest, "descriptor syntax error: " + desc_file.string() + ":" +
                                   std::to_string(err.source().begin.line) + ":" +
                                   std::to_string(err.source().begin.column) + ": " + std::string(err.description()));
  }

  // Available toolkits for descriptor loading (required toolkits + installed optional toolkits if __all__)
  std::vector<std::string> available_toolkits = required_toolkits;
  if (has_all) {
    available_toolkits.push_back("__all__");
    for (const auto& k : known) {
      const fs::path tk_dir = repo_root / "engine" / "toolkit" / k.name;
      if (fs::is_regular_file(tk_dir / "toolkit.toml")) add_unique(available_toolkits, k.name);
    }
  }

  step_manifest.duration_seconds = seconds_since(t_start);
  step_manifest.ok = true;
  step_manifest.message = "format 0, " + manifest.descriptor + ", " + manifest.language;
  if (!manifest.solver.empty()) {
    step_manifest.message += ", solver " + manifest.solver;
  }
  finish(step_manifest);
  res.manifest_ok = true;

  // -------------------------------------------------------------
  // Stage 2: Catalog & Block Completeness
  // -------------------------------------------------------------
  CheckStep step_catalog{"catalog", false, false, "", 0.0};
  begin(step_catalog);
  t_start = std::chrono::steady_clock::now();

  bool catalog_success = false;
  try {
    toolkit::DescriptorOptions desc_opts;
    desc_opts.optional_imports = available_toolkits;
    const auto catalog = toolkit::load_descriptor_file(desc_file, toolkit::builtins(), desc_opts);
    res.domain = catalog.domain.empty() ? res.domain : catalog.domain;
    res.block_count = static_cast<int>(catalog.entries.size());
    res.object_count = static_cast<int>(catalog.objects.size());
    res.has_starter_graph = !catalog.graph.empty();
    res.has_starter_station = !catalog.station.empty();
    catalog_success = true;
  } catch (const toolkit::DescriptorError& err) {
    std::string msg = err.what();
    if (msg.find("the catalog is empty") != std::string::npos && fs::is_directory(dir / "domain")) {
      res.warnings.push_back("domain defines its blocks in code: " + msg);
      catalog_success = true;
    } else if (msg.find("no block type") != std::string::npos || msg.find("no station template") != std::string::npos) {
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
        // The domain's blocks and station objects are registered in C++ (or Python), which a
        // static load cannot see: ask the built backend what it offers.
        bool counted = false;
        if (manifest.language == "cpp" && ensure_built(dir, false, false, nullptr) && fs::is_regular_file(dir / manifest.run)) {
          const auto probe = ProcessRunner::run_sync({(dir / manifest.run).string(), "--catalog"}, dir);
          try {
            const auto got = nlohmann::json::parse(probe.stdout_text);
            res.block_count = static_cast<int>(got.at("blocks").size());
            res.object_count = static_cast<int>(got.at("objects").size());
            counted = probe.ok();
          } catch (const std::exception&) {
          }
        }
        if (!counted) res.warnings.push_back("domain block/station template provided by runtime (counts need a built backend): " + msg);
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

  step_catalog.duration_seconds = seconds_since(t_start);

  if (!catalog_success) {
    step_catalog.ok = false;
    finish(step_catalog);
    return res;
  }

  // Detect whether built-in solvers API is used by the backend
  bool uses_solvers = !manifest.solver.empty();
  if (!uses_solvers && catalog_success) {
    try {
      toolkit::DescriptorOptions desc_opts;
      desc_opts.optional_imports = available_toolkits;
      const auto cat = toolkit::load_descriptor_file(desc_file, toolkit::builtins(), desc_opts);
      for (const auto& entry : cat.entries) {
        if (entry.def.type == "integrator" || entry.def.type == "lag") { uses_solvers = true; break; }
        for (const auto& p : entry.def.params) {
          if (p.id == "solver") { uses_solvers = true; break; }
        }
        if (uses_solvers) break;
      }
      if (!uses_solvers) {
        for (const auto& obj : cat.objects) {
          for (const auto& p : obj.def.props) {
            if (p.id == "solver") { uses_solvers = true; break; }
          }
          if (uses_solvers) break;
        }
      }
    } catch (...) {}
  }
  if (!uses_solvers) {
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
      if (!entry.is_regular_file()) continue;
      const auto ext = entry.path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".py") continue;
      const auto pstr = entry.path().string();
      if (pstr.find("/build/") != std::string::npos || pstr.find("/.") != std::string::npos) continue;
      std::ifstream in(entry.path());
      std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (content.find("paperboat::solve") != std::string::npos ||
          content.find("paperboat.solve") != std::string::npos ||
          content.find("paperboat::Model") != std::string::npos ||
          content.find("paperboat.Model") != std::string::npos ||
          content.find("paperboat/toolkit/solve.hpp") != std::string::npos) {
        uses_solvers = true;
        break;
      }
    }
  }

  if (uses_solvers) {
    if (res.solver.empty()) {
      res.solver = "rk4";
    }
    try {
      auto s = toolkit::solve::make(res.solver);
    } catch (const std::exception& err) {
      return fail(step_catalog, "failed to initialize solver " + res.solver + ": " + err.what());
    }
  }

  step_catalog.ok = true;
  step_catalog.message = std::to_string(res.block_count) + " blocks, " +
                         std::to_string(res.object_count) + " station objects";
  if (!res.solver.empty()) {
    step_catalog.message += ", solver " + res.solver;
  }
  finish(step_catalog);
  res.catalog_ok = true;

  // -------------------------------------------------------------
  // If fast mode, skip stages 3 & 4
  // -------------------------------------------------------------
  if (fast) {
    CheckStep step_smoke{"smoke", false, true, "skipped (--fast)", 0.0};
    finish(step_smoke);
    CheckStep step_golden{"golden", false, true, "skipped (--fast)", 0.0};
    finish(step_golden);
    res.ok = res.errors.empty();
    return res;
  }

  // -------------------------------------------------------------
  // Stage 3: Live Protocol Smoke Test
  // -------------------------------------------------------------
  CheckStep step_smoke{"smoke", false, false, "", 0.0};
  begin(step_smoke);
  t_start = std::chrono::steady_clock::now();

  auto py_env = prepare_python_env(repo_root, dir);
  if (manifest.language == "cpp") {
    std::string build_err;
    if (!ensure_built(dir, false, false, &build_err)) {
      return fail(step_smoke, "build failed: " + build_err);
    }
  }

  int smoke_port = find_free_port(18765, 18999);
  if (smoke_port <= 0) {
    return fail(step_smoke, "no ephemeral port available for smoke test");
  }

  // Construct launch command
  std::vector<std::string> launch_cmd;
  if (manifest.language == "cpp") {
    fs::path bin = dir / manifest.run;
    if (!fs::is_regular_file(bin)) {
      return fail(step_smoke, "executable not found: " + bin.string());
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
    return fail(step_smoke, "failed to start backend process");
  }

  struct BackendCleaner {
    int pid;
    int port;
    ~BackendCleaner() {
      if (pid > 0) {
#ifdef _WIN32
        ProcessRunner::kill_pgid(pid, SIGTERM, 300);
#else
        kill(-pid, SIGTERM);
        kill(pid, SIGTERM);
#endif
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
    return fail(step_smoke, "backend failed to start on port " + std::to_string(smoke_port) +
                                (err_output.empty() ? "" : ": " + err_output));
  }

  fs::path smoke_script = repo_root / "protocol" / "smoke_test.py";
  auto smoke_res = ProcessRunner::run_sync(
      {py_env.python_bin.string(), smoke_script.string(), "--port", std::to_string(smoke_port)},
      repo_root, py_env.env, std::chrono::milliseconds(20000));

  step_smoke.duration_seconds = seconds_since(t_start);

  if (!smoke_res.ok()) {
    std::string failure_msg = output_of(smoke_res);
    if (failure_msg.empty()) failure_msg = "smoke test failed (exit code " + std::to_string(smoke_res.exit_code) + ")";
    return fail(step_smoke, failure_msg);
  }

  step_smoke.ok = true;
  step_smoke.message = "protocol smoke test passed on port " + std::to_string(smoke_port);
  finish(step_smoke);
  res.smoke_ok = true;

  // -------------------------------------------------------------
  // Stage 4: Golden Headless Comparison
  // -------------------------------------------------------------
  CheckStep step_golden{"golden", false, false, "", 0.0};
  begin(step_golden);
  t_start = std::chrono::steady_clock::now();

  if (manifest.golden.empty()) {
    step_golden.skipped = true;
    step_golden.ok = true;
    step_golden.message = "no golden test declared";
    finish(step_golden);
    res.golden_ok = true;
  } else {
    fs::path golden_script = repo_root / "protocol" / "golden.py";
    auto golden_res = ProcessRunner::run_sync(
        {py_env.python_bin.string(), golden_script.string(), dir.string()},
        repo_root, py_env.env, std::chrono::milliseconds(30000));

    step_golden.duration_seconds = seconds_since(t_start);

    if (!golden_res.ok()) {
      std::string failure_msg = output_of(golden_res);
      if (failure_msg.empty()) failure_msg = "golden test failed (exit code " + std::to_string(golden_res.exit_code) + ")";
      return fail(step_golden, failure_msg);
    }

    step_golden.ok = true;
    step_golden.message = manifest.golden + " matched";
    finish(step_golden);
    res.golden_ok = true;
  }

  res.ok = res.errors.empty();
  return res;
}

}  // namespace paperboat::core
