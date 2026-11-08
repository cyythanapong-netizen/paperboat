#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
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
  std::string project_file = "";
  bool run = false;
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

struct BackendManifest {
  int format = 0;
  std::string name;
  std::string version = "0.1.0";
  std::string engine = ">=0.1";
  std::string language;  // "cpp" or "python"
  std::string run;       // path to binary or script relative to backend root
  int port = 8765;
  std::string descriptor = "descriptor.toml";
  std::vector<std::string> toolkits;
  std::string golden;    // from [test].golden
  std::string solver;    // default solver: "rk4", "rk45", "rk2", "euler", "discrete"
  // [run] solver and [run.solver_options] as graph.run settings JSON ("" if neither); the
  // backend gets it as PAPERBOAT_RUN and announces it in hello.run.
  std::string run_settings;
  bool has_manifest = false;
};

struct CheckStep {
  std::string name;       // "manifest", "catalog", "smoke", "golden"
  bool ok = false;
  bool skipped = false;
  std::string message;
  double duration_seconds = 0.0;
};

struct CheckResult {
  bool ok = false;
  std::string name;
  std::string version = "0.1.0";
  std::string language;
  std::string domain;
  std::string engine;
  std::string solver;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  std::vector<std::string> imported_packages;
  std::vector<std::string> missing_toolkits;
  int block_count = 0;
  int object_count = 0;
  bool has_starter_graph = false;
  bool has_starter_station = false;
  std::vector<CheckStep> steps;
  bool manifest_ok = false;
  bool catalog_ok = false;
  bool smoke_ok = false;
  bool golden_ok = false;
};

// Called when a check stage starts (`done` false) and when it ends (`done` true).
using CheckProgress = std::function<void(const CheckStep& step, bool done)>;
// Called per file packed or extracted: files done, files in all, bytes so far.
using PackProgress = std::function<void(int files, int total, uint64_t bytes)>;

struct PackResult {
  bool ok = false;
  std::filesystem::path package_path;
  std::string name;
  std::string version;
  int file_count = 0;
  uint64_t total_bytes = 0;
  std::string sha256;
  std::vector<std::string> errors;
};

struct UnpackResult {
  bool ok = false;
  std::filesystem::path destination_dir;
  std::string name;
  std::string version;
  int file_count = 0;
  std::vector<std::string> errors;
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
  static std::filesystem::path user_backends_dir();
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
  static BackendManifest load_manifest(const std::filesystem::path& backend_dir, std::string* error_msg = nullptr);
  static int find_free_port(int start_port = 18765, int end_port = 18999);
  static CheckResult check_backend(const std::filesystem::path& path = {}, bool fast = false,
                                   const CheckProgress& progress = {});

  // Packaging & unpack
  static PackResult pack_backend(const std::filesystem::path& path = {},
                                 const std::filesystem::path& out_path = {},
                                 bool fast = true,
                                 const PackProgress& progress = {});
  static UnpackResult unpack_backend(const std::filesystem::path& archive_path,
                                     const std::filesystem::path& dest_dir = {},
                                     bool verify = true,
                                     const PackProgress& progress = {});
};

}  // namespace paperboat::core
