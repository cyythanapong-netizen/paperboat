#include <fstream>
#include <mutex>
#include <thread>

#include "paperboat/core/backend.hpp"

namespace paperboat::core {

struct Session::State {
  State(const Backend& b, Send s)
      : backend(b), send(std::move(s)), station(b.catalog->station.empty() ? json() : json::parse(b.catalog->station)) {}

  const Backend& backend;
  Send send;
  std::mutex mutex;  // guards keys and station: the socket thread writes, the run thread reads
  toolkit::KeySet keys;
  json station;
  int runs = 0;
  std::jthread run;  // declared last: destroyed (stopped and joined) before the rest

  void emit(const json& msg) { send(msg.dump()); }
  void log(const std::string& level, const std::string& text) {
    emit({{"type", "log"}, {"level", level}, {"text", text}});
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
  const auto opening = backend.state ? backend.state() : nullptr;
  s.emit(scene_message(make_station(s.station, catalog), opening.get(), backend.view));
  s.emit({{"type", "graph.load"}, {"graph", json::parse(catalog.graph)}});
  if (!s.station.is_null()) s.emit({{"type", "station.load"}, {"station", s.station}});
}

Session::~Session() = default;

void Session::on_message(const std::string& text) {
  auto& s = *state_;
  const json msg = json::parse(text, nullptr, /*allow_exceptions=*/false);
  const std::string type = msg.is_object() ? msg.value("type", "") : "";
  if (type == "graph.run") {
    json station;
    {
      std::lock_guard lock(s.mutex);
      if (msg.contains("station")) s.station = msg.at("station");
      station = s.station;
    }
    const std::string id = "run-" + std::to_string(++s.runs);
    auto keys_now = [&s] {
      std::lock_guard lock(s.mutex);
      return s.keys;
    };
    // Assigning a new jthread stops and joins the previous run first.
    s.run = std::jthread([&s, id, graph = msg.value("graph", json::object()), station, keys_now,
                          settings = msg.value("settings", json())](std::stop_token stop) {
      core::run(id, graph, station, s.backend, [&s](const json& m) { s.emit(m); }, keys_now, stop, settings);
    });
  } else if (type == "station.set") {
    // An edit on the Station page: check it, keep it, and show it.
    try {
      const json station = msg.at("station");
      const auto objects = make_station(station, *s.backend.catalog);
      {
        std::lock_guard lock(s.mutex);
        s.station = station;
      }
      const auto state = s.backend.state ? s.backend.state() : nullptr;
      s.emit(scene_message(objects, state.get(), s.backend.view));
    } catch (const std::exception& err) {
      s.log("error", std::string("invalid station: ") + err.what());
    }
  } else if (type == "run.cancel") {
    s.run.request_stop();
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
