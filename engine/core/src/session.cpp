#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/cache.hpp"

#include <paperboat/toolkit/installer.hpp>
#include <paperboat/toolkit/platform.hpp>
#include <paperboat/toolkit/variables.hpp>

namespace paperboat::core {

struct Session::State {
  State(const Backend& b, Send s)
      : backend(b),
        send(std::move(s)),
        station(!b.initial_project.is_null() && b.initial_project.contains("station")
                    ? b.initial_project["station"]
                    : (b.catalog->station.empty() ? json() : json::parse(b.catalog->station))),
        variables(!b.initial_project.is_null() && b.initial_project.contains("variables")
                      ? b.initial_project["variables"]
                      : (b.catalog->variables.empty() ? json::array() : json::parse(b.catalog->variables))) {}

  const Backend& backend;
  Send send;
  std::mutex mutex;  // guards keys, station and variables: the socket thread writes, the run thread reads
  toolkit::KeySet keys;
  toolkit::KeySet pressed;  // pressed since the run last read the keys: a short tap still shows for one pass
  json station;
  json variables;  // the list (#36): the backend's defaults until the UI sets its own
  std::optional<json> tuned;  // the list the UI sent during a run (#75), until the run takes it
  std::optional<double> speed;  // the speed the UI asked for during a run (run.speed, #219), until the run takes it
  int runs = 0;
  std::atomic<int> live_run{0};  // the number of the run that is going, else 0
  RunCache cache;  // what runs that evaluate once keep for the next run of this session
  std::mutex toolkit_mutex;
  std::string busy_toolkit;
  std::string busy_action;
  std::jthread toolkit_task;
  std::jthread run;  // declared last: destroyed (stopped and joined) before the rest

  void emit(const json& msg) { send(msg.dump()); }
  void log(const std::string& level, const std::string& text) {
    emit({{"type", "log"}, {"level", level}, {"text", text}});
  }
  // The build this backend runs from: the nearest folder above its executable with a CMakeCache.txt,
  // found at run time (no build-machine path is compiled in, pb#177).
  toolkit::HostBuild host_build() const {
    std::error_code ec;
    for (auto dir = platform::exe_path().parent_path(); !dir.empty(); dir = dir.parent_path()) {
      if (std::filesystem::is_regular_file(dir / "CMakeCache.txt", ec)) return {.source_dir = {}, .build_dir = dir};
      if (dir == dir.parent_path()) break;
    }
    return {};
  }
  std::filesystem::path repo_root() const { return toolkit::find_repo_root(backend.catalog->dir); }
  json current_toolkits(std::string_view busy_name = {}, std::string_view busy_state = {}, bool fetch_versions = false) {
    std::string bname(busy_name);
    std::string bstate(busy_state);
    if (bname.empty()) {
      std::lock_guard lock(toolkit_mutex);
      bname = busy_toolkit;
      bstate = busy_action;
    }
    const auto list = toolkit::list_toolkits(repo_root(), backend.catalog->dir, fetch_versions);
    return json::parse(toolkit::toolkits_json(list, bname, bstate));
  }
  void emit_toolkits(std::string_view busy_name = {}, std::string_view busy_state = {}, bool fetch_versions = false) {
    emit({{"type", "toolkits.status"}, {"toolkits", current_toolkits(busy_name, busy_state, fetch_versions)}});
  }
  // The station as it shows: its objects made with their references resolved. Throws
  // VariableError, GraphError or json::exception.
  // `report`: also send what the variables come to (variables.values), for the UI.
  void show(const json& with_station, const json& with_variables, bool report = false) {
    const toolkit::Variables vars(with_variables);
    if (report) emit({{"type", "variables.values"}, {"values", vars.values()}});
    const auto objects = make_station(toolkit::resolve_station(with_station, *backend.catalog, vars), *backend.catalog);
    const auto state = backend.state ? backend.state() : nullptr;
    emit(scene_message(objects, state.get(), backend.view));
  }
};

Session::Session(const Backend& backend, Send send)
    : state_(std::make_unique<State>(backend, std::move(send))) {
  auto& s = *state_;
  const auto& catalog = *backend.catalog;
  json station_view = {{"view", catalog.station_view}};
  if (!catalog.station_page.empty()) station_view["page"] = "/" + catalog.station_page;  // on this port, over HTTP
  json hello = {{"type", "hello"},
                {"protocol", "paperboat/0"},
                {"engine", backend.engine},
                {"station", station_view},
                {"run", settings_json(backend.run)},
                {"flows", catalog.flows}};  // the flow types a sheet may be (#9)
  const json toolkits = s.current_toolkits(/*busy_name=*/{}, /*busy_state=*/{}, /*fetch_versions=*/true);
  if (!toolkits.empty()) hello["toolkits"] = toolkits;
  s.emit(hello);
  s.emit(json::parse(toolkit::catalog_message(catalog)));
  try {
    s.show(s.station, s.variables, !s.variables.empty());  // checked at load: the starters resolve against the defaults
  } catch (const std::exception& err) {
    if (catalog.missing_imports.empty()) {
      s.log("error", std::string("cannot show station: ") + err.what());
    }
  }
  if (!s.variables.empty()) s.emit({{"type", "variables.load"}, {"variables", s.variables}});
  s.emit({{"type", "graph.load"}, {"graph", json::parse(catalog.graph)}});
  if (!s.station.is_null()) s.emit({{"type", "station.load"}, {"station", s.station}});
  if (!backend.initial_project.is_null() && backend.initial_project.is_object()) {
    s.emit({{"type", "project.load"},
            {"name", backend.initial_project_name},
            {"project", backend.initial_project}});
  }
}

Session::~Session() = default;

void Session::on_message(const std::string& text) {
  auto& s = *state_;
  const json msg = json::parse(text, nullptr, /*allow_exceptions=*/false);
  const std::string type = msg.is_object() ? msg.value("type", "") : "";
  if (type == "graph.run") {
    if (!s.backend.catalog->missing_imports.empty()) {
      std::string missing;
      for (const auto& m : s.backend.catalog->missing_imports) missing += (missing.empty() ? "" : ", ") + m;
      s.log("error", "Cannot run: missing required toolkits (" + missing + "). Please install them first.");
      s.emit({{"type", "run.finished"}, {"run", "run-" + std::to_string(s.runs + 1)}, {"status", "failed"}});
      return;
    }
    json station, variables;
    {
      std::lock_guard lock(s.mutex);
      if (msg.contains("station")) s.station = msg.at("station");
      if (msg.contains("variables")) s.variables = msg.at("variables");
      station = s.station;
      variables = s.variables;
      s.pressed.clear();  // presses from before this run don't count
      s.tuned.reset();    // nor changes to the variables from before it
      s.speed.reset();    // nor a speed asked of an earlier run
    }
    const int number = ++s.runs;
    const std::string id = "run-" + std::to_string(number);
    auto tune_now = [&s]() -> std::optional<json> {
      std::lock_guard lock(s.mutex);
      return std::exchange(s.tuned, std::nullopt);
    };
    auto speed_now = [&s]() -> std::optional<double> {
      std::lock_guard lock(s.mutex);
      return std::exchange(s.speed, std::nullopt);
    };
    s.live_run = number;
    auto keys_now = [&s] {
      std::lock_guard lock(s.mutex);
      auto down = s.keys;
      down.merge(s.pressed);
      s.pressed.clear();
      return down;
    };
    // Stop and join the previous run before starting this one. A new jthread starts as soon as it
    // is constructed, while the old one may still be emitting or pruning the cache.
    if (s.run.joinable()) {
      s.run.request_stop();
      s.run.join();
    }
    s.run = std::jthread([&s, id, number, graph = msg.value("graph", json::object()), station, variables, keys_now, tune_now,
                          speed_now, settings = msg.value("settings", json())](std::stop_token stop) {
      auto emit = [&s, number](const json& m) {
        if (m.value("type", "") == "variables.patch") {  // a block set variables (#166): later runs start from them
          std::lock_guard lock(s.mutex);
          for (json* list : {&s.variables, s.tuned ? &*s.tuned : nullptr}) {
            if (!list || !list->is_array()) continue;
            for (auto& v : *list)
              if (const auto it = m.at("values").find(v.value("name", std::string{})); it != m.at("values").end() && !v.contains("expr")) v["value"] = *it;
          }
        }
        if (m.value("type", "") == "run.finished") {  // before the UI hears it: its next edit is not a tuning
          int mine = number;
          s.live_run.compare_exchange_strong(mine, 0);  // not if a newer run has started meanwhile
        }
        s.emit(m);
      };
      core::run(id, graph, station, s.backend, emit, keys_now, stop, settings, variables, tune_now, speed_now, &s.cache);
    });
  } else if (type == "station.set") {
    // An edit on the Station page: check it, keep it, and show it.
    try {
      const json station = msg.at("station");
      json variables;
      {
        std::lock_guard lock(s.mutex);
        variables = s.variables;
      }
      s.show(station, variables);
      std::lock_guard lock(s.mutex);
      s.station = station;
    } catch (const std::exception& err) {
      s.log("error", std::string("invalid station: ") + err.what());
    }
  } else if (type == "variables.set") {
    // The variables changed (#36): check them, keep them, and show the station they shape.
    try {
      const json variables = msg.at("variables");
      if (s.live_run != 0) {
        // During a run (#75): the run owns the scene, so it isn't shown again. Keep the list for
        // the next run and hand it to this one, which takes the tunable variables' values.
        s.emit({{"type", "variables.values"}, {"values", toolkit::Variables(variables).values()}});
        std::lock_guard lock(s.mutex);
        s.variables = variables;
        s.tuned = variables;
        return;
      }
      json station;
      {
        std::lock_guard lock(s.mutex);
        station = s.station;
      }
      s.show(station, variables, true);
      std::lock_guard lock(s.mutex);
      s.variables = variables;
    } catch (const toolkit::VariableError& err) {
      s.emit({{"type", "variables.values"}, {"values", json::object()}, {"error", err.what()}, {"at", err.where}});
      s.log("error", std::string("invalid variables: ") + err.what());
    } catch (const std::exception& err) {
      s.log("error", std::string("invalid variables: ") + err.what());
    }
  } else if (type == "run.cancel") {
    s.run.request_stop();
  } else if (type == "run.speed") {
    // The speed changed during a run (#219): the run takes it at its next frame and answers with
    // a run.speed of its own. Between runs there is nothing to change (the next graph.run brings
    // its speed): ignored, as a change that crossed the run's end on the wire.
    double speed = 0;
    try {
      speed = checked_speed(msg.contains("speed") ? msg.at("speed") : json());
    } catch (const std::invalid_argument& err) {
      s.log("error", std::string("run.speed: ") + err.what());
      return;
    }
    if (s.live_run == 0) return;
    std::lock_guard lock(s.mutex);
    s.speed = speed;
  } else if (type == "input") {  // read by Key and Switch blocks
    const std::string key = msg.value("key", "");
    std::lock_guard lock(s.mutex);
    if (msg.value("down", false)) {
      s.keys.insert(key);
      s.pressed.insert(key);
    } else {
      s.keys.erase(key);
    }
  } else if (type == "backend.restart") {
    s.log("info", "restarting backend…");
    const auto dir = s.backend.catalog ? s.backend.catalog->dir : std::filesystem::path{};
    std::thread([dir] {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!dir.empty() && std::filesystem::is_regular_file(dir / "CMakeLists.txt")) {
        std::string err;
        BackendManager::ensure_built(dir, false, false, &err);
      }
      restart_process();
    }).detach();
  } else if (type == "toolkit.install" || type == "toolkit.uninstall") {
    const std::string name = msg.value("name", "");
    const std::string version = msg.value("version", "");
    if (name.empty()) {
      s.log("error", type + ": toolkit name is required");
      return;
    }
    const std::string busy = type == "toolkit.install" ? "installing" : "uninstalling";
    {
      std::lock_guard lock(s.toolkit_mutex);
      s.busy_toolkit = name;
      s.busy_action = busy;
    }
    s.toolkit_task = std::jthread([&s, type, name, version, busy]() {
      s.emit_toolkits();
      auto logger = [&s](std::string_view level, std::string_view line) {
        s.log(std::string(level), std::string(line));
      };
      toolkit::ToolkitResult res;
      if (type == "toolkit.install") res = toolkit::install_toolkit(name, version, s.repo_root(), s.host_build(), logger);
      else res = toolkit::uninstall_toolkit(name, s.repo_root(), s.host_build(), logger);
      if (!res.ok) s.log("error", res.message);
      {
        std::lock_guard lock(s.toolkit_mutex);
        s.busy_toolkit.clear();
        s.busy_action.clear();
      }
      s.emit_toolkits();
    });
  } else if (type == "toolkit.versions") {
    std::thread([&s]() {
      s.emit_toolkits(/*busy_name=*/{}, /*busy_state=*/{}, /*fetch_versions=*/true);
    }).detach();
  } else {
    s.log("warn", "unknown message: " + type);
  }
}

json load_json(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  return json::parse(in);
}

int port_arg(int argc, char** argv, int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--port") return std::stoi(argv[i + 1]);
  }
  return fallback;
}

}  // namespace paperboat::core
