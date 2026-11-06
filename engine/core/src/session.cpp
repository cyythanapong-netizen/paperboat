#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "paperboat/core/backend.hpp"

#include <paperboat/toolkit/installer.hpp>
#include <paperboat/toolkit/variables.hpp>

namespace paperboat::core {

struct Session::State {
  State(const Backend& b, Send s)
      : backend(b),
        send(std::move(s)),
        station(b.catalog->station.empty() ? json() : json::parse(b.catalog->station)),
        variables(b.catalog->variables.empty() ? json::array() : json::parse(b.catalog->variables)) {}

  const Backend& backend;
  Send send;
  std::mutex mutex;  // guards keys, station and variables: the socket thread writes, the run thread reads
  toolkit::KeySet keys;
  json station;
  json variables;  // the list (#36): the backend's defaults until the UI sets its own
  int runs = 0;
  std::mutex toolkit_mutex;
  std::string busy_toolkit;
  std::string busy_action;
  std::jthread toolkit_task;
  std::jthread run;  // declared last: destroyed (stopped and joined) before the rest

  void emit(const json& msg) { send(msg.dump()); }
  void log(const std::string& level, const std::string& text) {
    emit({{"type", "log"}, {"level", level}, {"text", text}});
  }
  toolkit::HostBuild host_build() const {
#if defined(PAPERBOAT_HOST_SOURCE_DIR) && defined(PAPERBOAT_HOST_BUILD_DIR)
    return {PAPERBOAT_HOST_SOURCE_DIR, PAPERBOAT_HOST_BUILD_DIR};
#else
    return {};
#endif
  }
  std::filesystem::path repo_root() const {
#if defined(PAPERBOAT_REPO_ROOT)
    return PAPERBOAT_REPO_ROOT;
#else
    return {};
#endif
  }
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
                {"run", settings_json(backend.run)}};
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
    }
    const std::string id = "run-" + std::to_string(++s.runs);
    auto keys_now = [&s] {
      std::lock_guard lock(s.mutex);
      return s.keys;
    };
    // Assigning a new jthread stops and joins the previous run first.
    s.run = std::jthread([&s, id, graph = msg.value("graph", json::object()), station, variables, keys_now,
                          settings = msg.value("settings", json())](std::stop_token stop) {
      core::run(id, graph, station, s.backend, [&s](const json& m) { s.emit(m); }, keys_now, stop, settings, variables);
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
  } else if (type == "input") {  // read by Key blocks
    const std::string key = msg.value("key", "");
    std::lock_guard lock(s.mutex);
    if (msg.value("down", false)) s.keys.insert(key);
    else s.keys.erase(key);
  } else if (type == "backend.restart") {
    s.log("info", "restarting backend…");
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
