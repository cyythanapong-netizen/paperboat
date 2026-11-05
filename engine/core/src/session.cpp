#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "paperboat/core/backend.hpp"

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
  std::jthread run;  // declared last: destroyed (stopped and joined) before the rest

  void emit(const json& msg) { send(msg.dump()); }
  void log(const std::string& level, const std::string& text) {
    emit({{"type", "log"}, {"level", level}, {"text", text}});
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
  s.emit({{"type", "hello"}, {"protocol", "paperboat/0"}, {"engine", backend.engine}, {"station", station_view}, {"run", settings_json(backend.run)}});
  s.emit(json::parse(toolkit::catalog_message(catalog)));
  s.show(s.station, s.variables, !s.variables.empty());  // checked at load: the starters resolve against the defaults
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
  } else if (type == "backend.restart") {
    s.log("info", "restarting backend…");
    // Let the message leave before replacing the process.
    std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); restart_backend(); }).detach();
  } else if (type == "toolkit.install" || type == "toolkit.uninstall") {
    const std::string name = msg.value("name", "opencv");
    const bool install = type == "toolkit.install";
    s.log("info", (install ? "installing" : "uninstalling") + std::string(" toolkit ") + name + "…");
    std::thread([&, name, install] {
      std::filesystem::path root = std::filesystem::current_path();
      for (int i = 0; i < 8 && !(std::filesystem::exists(root / "scripts" / "install_toolkit.py")); ++i) root = root.parent_path();
      if (install) {
        std::string cmd = "python3 " + (root / "scripts" / "install_toolkit.py").string() + " " + name;
        if (std::system(cmd.c_str()) != 0) { s.log("error", "toolkit " + name + " install failed"); return; }
      } else {
        std::error_code ec;
        std::filesystem::remove_all(root / "engine" / "toolkit" / name, ec);
        if (ec) { s.log("error", "toolkit " + name + " uninstall failed: " + ec.message()); return; }
      }
      std::string cfg = "cmake -S " + (root / "engine").string() + " -B " + (root / "engine" / "build").string() + " -G Ninja -DCMAKE_BUILD_TYPE=Debug";
      if (std::system(cfg.c_str()) != 0) { s.log("error", "toolkit cmake configure failed"); return; }
      std::string build = "cmake --build " + (root / "engine" / "build").string();
      if (std::system(build.c_str()) != 0) { s.log("error", "toolkit cmake build failed"); return; }
      s.log("info", "toolkit " + name + (install ? " installed" : " uninstalled") + "; restart the backend to apply it");
    }).detach();
  } else if (type == "input") {  // read by Key blocks
    const std::string key = msg.value("key", "");
    std::lock_guard lock(s.mutex);
    if (msg.value("down", false)) s.keys.insert(key);
    else s.keys.erase(key);
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
