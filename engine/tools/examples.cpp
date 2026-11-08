#include "examples.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/installer.hpp"
#include "paperboat/toolkit/platform.hpp"

namespace paperboat::cli {

namespace fs = std::filesystem;
using core::BackendManager;

std::vector<Example> list_examples(const fs::path& dir) {
  std::vector<Example> list;
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) return list;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_directory() || !fs::is_regular_file(entry.path() / "backend.toml")) continue;
    const auto m = BackendManager::load_manifest(entry.path());
    if (!m.has_manifest) continue;
    Example ex{m.name.empty() ? entry.path().filename().string() : m.name, m.language, m.port, entry.path()};
    if (BackendManager::is_port_listening(ex.port)) {
      ex.running = true;
      ex.pid = BackendManager::find_pid_by_port(ex.port);
    }
    list.push_back(std::move(ex));
  }
  std::ranges::sort(list, {}, &Example::port);
  return list;
}

fs::path find_examples_dir(const fs::path& hint) {
  std::vector<fs::path> candidates;
  if (!hint.empty()) candidates.push_back(hint);
  if (const char* env = std::getenv("PAPERBOAT_EXAMPLES_DIR"); env && *env) candidates.emplace_back(env);
  if (const fs::path root = toolkit::find_repo_root(); !root.empty()) {
    candidates.push_back(root.parent_path() / "paperboat-examples");
  }
  candidates.push_back(toolkit::apps_dir() / "paperboat-examples");
  for (const auto& c : candidates) {
    if (!list_examples(c).empty()) return fs::absolute(c);
  }
  return {};
}

bool start_examples(const fs::path& dir) {
  const auto examples = list_examples(dir);
  if (examples.empty()) {
    std::cerr << "error: no examples found. Set PAPERBOAT_EXAMPLES_DIR, pass --dir, or run "
                 "`paperboat pkg install paperboat-examples`.\n";
    return false;
  }
  std::cout << "Using examples directory: " << dir.string() << '\n';
  stop_examples(dir);

  for (const auto& ex : examples) {
    if (ex.language == "cpp") {  // build in the foreground, one at a time, so failures are seen
      std::cout << "==> Building " << ex.name << "…\n";
      std::string err;
      if (!BackendManager::ensure_built(ex.dir, false, false, &err)) {
        std::cerr << "warning: " << ex.name << " build failed: " << err << '\n';
        continue;
      }
    }
    // `paperboat run` serves it; the backend registers itself, so `list` and `stop` find it.
    const fs::path log = platform::shared_tmp_dir() / ("backend-" + std::to_string(ex.port) + ".log");
    const int pid = core::ProcessRunner::run_detached(
        {platform::exe_path().string(), "run", ex.dir.string(), "--port", std::to_string(ex.port)}, ex.dir, {}, log);
    if (pid > 0) std::cout << "Started " << ex.name << " on port " << ex.port << " (PID " << pid << ")\n";
    else std::cerr << "warning: could not start " << ex.name << '\n';
  }
  return true;
}

bool stop_examples(const fs::path& dir) {
  for (const auto& ex : list_examples(dir)) {
    BackendManager::free_port(ex.port, 1000);
    BackendManager::unregister_running_backend(ex.port);
  }
  std::cout << "==> All example backends stopped.\n";
  return true;
}

}  // namespace paperboat::cli
