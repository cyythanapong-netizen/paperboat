#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

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
    fs::path log_path = platform::shared_tmp_dir() / "paperboat-web.log";
    int pid = ProcessRunner::run_detached(args, root, {}, log_path);
    if (pid > 0) {
      std::cout << "Web application started in background (PID " << pid << ", log: " << log_path << ")\n";
      return true;
    }
    return false;
  } else {
    return ProcessRunner::run_stream(args, root, {}, [](std::string_view stream, std::string_view line) {
      (stream == "stderr" ? std::cerr : std::cout) << line << std::endl;
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

}  // namespace paperboat::core
