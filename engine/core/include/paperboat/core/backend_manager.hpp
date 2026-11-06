#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paperboat::core {

struct RunningBackend {
  int port = 0;
  int pid = 0;
  std::string engine;
  std::string domain;
  std::filesystem::path cwd;
  std::filesystem::path backend_dir;
  std::string executable;
  std::string ws_url;
  int64_t started_at = 0;
};

struct BackendRunOptions {
  int port = 0;  // 0: default for backend or 8765
  bool build = true;
  bool clean = false;
  bool time_metrics = false;
  bool verbose = false;
  std::vector<std::pair<std::string, std::string>> vars;
};

enum class BackendKind {
  Cpp,
  Python,
  Unknown
};

struct ExampleBackend {
  int port = 0;
  std::string name;
  std::string type;  // "cpp" or "python"
  std::filesystem::path dir;
  bool running = false;
  int pid = 0;
  std::string ws_url;
};

struct CheckResult {
  bool ok = false;
  std::string domain;
  std::string engine;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  std::vector<std::string> imported_packages;
  std::vector<std::string> missing_toolkits;
  int block_count = 0;
  int object_count = 0;
  bool has_starter_graph = false;
  bool has_starter_station = false;
};

class BackendManager {
 public:
  // Port inspection & cleanup
  static bool is_port_listening(int port);
  static int find_pid_by_port(int port);
  static bool free_port(int port, int timeout_ms = 2000);
  static bool stop_user_service(const std::string& unit_name);

  // Runtime registry (/tmp/paperboat-runtime/backend-<port>.json)
  static std::filesystem::path runtime_dir();
  static std::filesystem::path runtime_record_path(int port);
  static void register_running_backend(const RunningBackend& backend);
  static void unregister_running_backend(int port);
  static std::vector<RunningBackend> list_running_backends();

  // Incremental build for C++ backends
  static bool needs_rebuild(const std::filesystem::path& backend_dir,
                            const std::filesystem::path& build_dir = {});
  static bool ensure_built(const std::filesystem::path& backend_dir,
                           bool clean = false,
                           bool verbose = false,
                           std::string* error_msg = nullptr,
                           double* elapsed_seconds = nullptr);

  // Python environment resolution & C++ binding builder
  struct PythonEnv {
    std::filesystem::path python_bin;
    std::vector<std::pair<std::string, std::string>> env;
  };
  static PythonEnv prepare_python_env(const std::filesystem::path& repo_root = {},
                                     const std::filesystem::path& backend_dir = {});

  // Backend kind and directory resolution
  static BackendKind detect_backend_kind(const std::filesystem::path& path);
  static std::filesystem::path resolve_backend_dir(const std::filesystem::path& path_or_domain = {});

  // Lifecycle execution
  static int run_backend(const std::filesystem::path& path_or_domain = {},
                         const BackendRunOptions& opts = {});
  static bool stop_backend(const std::string& target, bool all = false);
  static bool restart_backend(const std::string& target, const BackendRunOptions& opts = {});

  // Multi-backend orchestration (examples)
  static std::filesystem::path find_examples_dir(const std::filesystem::path& hint = {});
  static std::vector<ExampleBackend> get_examples(const std::filesystem::path& examples_dir = {});
  static bool start_examples(const std::filesystem::path& examples_dir = {});
  static bool stop_examples();
  static std::vector<ExampleBackend> status_examples(const std::filesystem::path& examples_dir = {});

  // Web application management
  static bool start_app(int port = 3047, const std::string& host = "0.0.0.0", bool daemon = false);
  static bool stop_app(int port = 3047);
  static bool restart_app(int port = 3047, const std::string& host = "0.0.0.0");
  static bool build_app();

  // Verification & checking
  static CheckResult check_backend(const std::filesystem::path& path = {}, bool fast = false);
};

}  // namespace paperboat::core
