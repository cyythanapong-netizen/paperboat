// The transport: one Session per WebSocket connection, and plain HTTP on the same port for
// a web station (the backend's own page, GUIDELINES §6.4) and its bridge script. The only
// file that knows sockets.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>

#include "paperboat/toolkit/installer.hpp"
#include "paperboat/toolkit/platform.hpp"
#include "paperboat/core/backend.hpp"
#include "paperboat/core/backend_manager.hpp"

namespace paperboat::core {

extern const char* const kStationJs;  // web/station.js, embedded at build time

namespace {

namespace fs = std::filesystem;

std::string content_type(const fs::path& file) {
  static const std::map<std::string, std::string> types = {
      {".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
      {".mjs", "text/javascript; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
      {".json", "application/json"}, {".svg", "image/svg+xml"}, {".png", "image/png"},
      {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"},
      {".woff2", "font/woff2"}, {".wasm", "application/wasm"}, {".txt", "text/plain; charset=utf-8"}};
  auto it = types.find(file.extension().string());
  return it == types.end() ? "application/octet-stream" : it->second;
}

ix::HttpResponsePtr respond(int status, const std::string& type, std::string body) {
  ix::WebSocketHttpHeaders headers;
  headers["Content-Type"] = type;
  headers["Cache-Control"] = "no-cache";
  return std::make_shared<ix::HttpResponse>(status, status == 200 ? "OK" : "Not Found", ix::HttpErrorCode::Ok, headers,
                                            std::move(body));
}

std::string decode(const std::string& uri) {
  std::string out;
  for (std::size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size()) {
      out += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += uri[i];
    }
  }
  return out;
}

// GET /paperboat/station.js: the bridge. GET /<page's folder>/…: a file of the web station,
// only from inside that folder. Everything else is 404: the engine is not a file server.
ix::HttpResponsePtr http(const Backend& backend, const ix::HttpRequestPtr& request) {
  const std::string path = decode(request->uri.substr(0, request->uri.find('?')));
  if (path == "/paperboat/station.js") return respond(200, "text/javascript; charset=utf-8", kStationJs);
  const auto& catalog = *backend.catalog;
  if (catalog.station_page.empty() || path.size() < 2) return respond(404, "text/plain", "not found\n");
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(catalog.dir / fs::path(catalog.station_page).parent_path(), ec);
  const fs::path file = fs::weakly_canonical(catalog.dir / path.substr(1), ec);
  const auto inside = file.lexically_relative(root);
  if (ec || inside.empty() || *inside.begin() == ".." || !fs::is_regular_file(file))
    return respond(404, "text/plain", "not found\n");
  std::ifstream in(file, std::ios::binary);
  std::stringstream body;
  body << in.rdbuf();
  return respond(200, content_type(file), body.str());
}

}  // namespace

int serve_files(const fs::path& dir, int port, const std::string& host) {
  ix::initNetSystem();
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(dir, ec);
  ix::HttpServer server(port, host);
  server.setOnConnectionCallback([&root](const ix::HttpRequestPtr& request, const std::shared_ptr<ix::ConnectionState>&) {
    std::string path = decode(request->uri.substr(0, request->uri.find('?')));
    // The Backend dialog lists what is running, like `paperboat list --json` does.
    if (path == "/__paperboat/backends") {
      nlohmann::json out = nlohmann::json::array();
      for (const auto& b : BackendManager::list_running_backends()) {
        out.push_back({{"name", b.domain.empty() ? b.engine : b.domain},
                       {"engine", b.engine},
                       {"domain", b.domain},
                       {"port", b.port},
                       {"url", b.ws_url.empty() ? "ws://localhost:" + std::to_string(b.port) : b.ws_url}});
      }
      return respond(200, "application/json", out.dump());
    }
    // Toolkits installed into a release (pb#353): the web parts `toolkit install` bundled, which the
    // app imports at startup, and their files.
    const std::string tk_prefix = "/__paperboat/toolkits";
    if (path == tk_prefix) {
      nlohmann::json out = nlohmann::json::array();
      std::error_code te;
      for (const auto& d : fs::directory_iterator(toolkit::toolkits_dir(toolkit::find_repo_root()), te)) {
        const std::string name = d.path().filename().string();
        if (name.starts_with(".") || !fs::is_regular_file(d.path() / "web-dist" / "index.js", te)) continue;
        nlohmann::json t = {{"name", name}, {"js", tk_prefix + "/" + name + "/index.js"}};
        if (fs::is_regular_file(d.path() / "web-dist" / "index.css", te)) t["css"] = tk_prefix + "/" + name + "/index.css";
        out.push_back(t);
      }
      return respond(200, "application/json", out.dump());
    }
    if (path.starts_with(tk_prefix + "/")) {
      const std::string rest = path.substr(tk_prefix.size() + 1);  // <name>/<file>
      const auto slash = rest.find('/');
      std::error_code te;
      if (slash != std::string::npos) {
        const fs::path dist = fs::weakly_canonical(toolkit::toolkits_dir(toolkit::find_repo_root()) / rest.substr(0, slash) / "web-dist", te);
        const fs::path file = fs::weakly_canonical(dist / rest.substr(slash + 1), te);
        const auto in_dist = file.lexically_relative(dist);
        if (!te && !in_dist.empty() && *in_dist.begin() != ".." && fs::is_regular_file(file, te)) {
          std::ifstream in(file, std::ios::binary);
          std::stringstream body;
          body << in.rdbuf();
          return respond(200, content_type(file), body.str());
        }
      }
      return respond(404, "text/plain", "not found\n");
    }
    std::error_code e;
    fs::path file = fs::weakly_canonical(root / (path.size() > 1 ? path.substr(1) : "index.html"), e);
    const auto inside = file.lexically_relative(root);
    if (e || inside.empty() || *inside.begin() == "..") return respond(404, "text/plain", "not found\n");
    if (!fs::is_regular_file(file)) file = root / "index.html";  // the app's own routes
    std::ifstream in(file, std::ios::binary);
    if (!in) return respond(404, "text/plain", "not found\n");
    std::stringstream body;
    body << in.rdbuf();
    return respond(200, content_type(file), body.str());
  });
  if (auto [ok, error] = server.listen(); !ok) {
    std::cerr << "error: cannot serve the web app on port " << port << ": " << error << '\n';
    return 1;
  }
  server.start();
  std::cout << "Paperboat web app on http://localhost:" << port << "/  (Ctrl+C stops it)" << std::endl;
  server.wait();
  return 0;
}

struct Server::State {
  State(const Backend& b, int p) : backend(b), port(p), server(p, "127.0.0.1") {}
  const Backend& backend;
  int port;
  ix::HttpServer server;  // also upgrades WebSocket connections, on the same port
  std::mutex mutex;
  std::map<std::string, std::unique_ptr<Session>> sessions;  // by connection id
  // Live variables from a client that is not this project (Squero MX), shared by every session.
  std::string remote_source;
  json remote_variables = json::array();

  void publish_remote(const std::string& source, const json& variables) {
    std::vector<Session*> targets;
    {
      std::lock_guard lock(mutex);
      remote_source = source;
      remote_variables = variables;
      for (auto& [id, session] : sessions) targets.push_back(session.get());
    }
    const json message = {{"type", "variables.remote"}, {"source", "mx"}, {"variables", variables}};
    for (Session* session : targets) session->send_message(message);
  }

  void clear_remote(const std::string& source) {
    std::vector<Session*> targets;
    {
      std::lock_guard lock(mutex);
      if (remote_source != source) return;
      remote_source.clear();
      remote_variables = json::array();
      for (auto& [id, session] : sessions) targets.push_back(session.get());
    }
    const json message = {{"type", "variables.remote"}, {"source", "mx"}, {"variables", json::array()}};
    for (Session* session : targets) session->send_message(message);
  }
};

Server::Server(const Backend& backend, int port) : state_(std::make_unique<State>(backend, port)) {
  ix::initNetSystem();
  auto& s = *state_;
  s.server.setOnConnectionCallback(
      [&s](const ix::HttpRequestPtr& request, const std::shared_ptr<ix::ConnectionState>&) {
        const std::string path = decode(request->uri.substr(0, request->uri.find('?')));
        if (path == "/variables.remote") {
          json variables;
          {
            std::lock_guard lock(s.mutex);
            variables = s.remote_variables;
          }
          auto response = respond(200, "application/json", json{{"source", "mx"}, {"variables", variables}}.dump());
          response->headers["Access-Control-Allow-Origin"] = "*";
          return response;
        }
        return http(s.backend, request);
      });
  s.server.setOnClientMessageCallback(
      [&s](std::shared_ptr<ix::ConnectionState> conn, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        const std::string& id = conn->getId();
        if (msg->type == ix::WebSocketMessageType::Open) {
          // sendText is thread-safe in IXWebSocket, so runs can send from their own thread.
          auto session = std::make_unique<Session>(
              s.backend, [&ws](const std::string& text) { ws.sendText(text); },
              [&s, id](const json& variables) { s.publish_remote(id, variables); });
          Session* created = nullptr;
          json remote_snapshot;
          {
            std::lock_guard lock(s.mutex);
            created = session.get();
            s.sessions[id] = std::move(session);
            remote_snapshot = s.remote_variables;
          }
          // A client that opens after MX sent its variables gets them too.
          if (created && !remote_snapshot.empty())
            created->send_message({{"type", "variables.remote"}, {"source", "mx"}, {"variables", remote_snapshot}});
        } else if (msg->type == ix::WebSocketMessageType::Close) {
          std::unique_ptr<Session> gone;  // destroyed outside the lock: joins its run
          {
            std::lock_guard lock(s.mutex);
            if (auto it = s.sessions.find(id); it != s.sessions.end()) gone = std::move(it->second), s.sessions.erase(it);
          }
          s.clear_remote(id);
        } else if (msg->type == ix::WebSocketMessageType::Message) {
          Session* session = nullptr;
          {
            std::lock_guard lock(s.mutex);
            if (auto it = s.sessions.find(id); it != s.sessions.end()) session = it->second.get();
          }
          if (session) session->on_message(msg->str);  // each connection has its own callback thread
        }
      });
}

Server::~Server() { stop(); }

std::string Server::start() {
  if (auto [ok, error] = state_->server.listen(); !ok) return error;
  state_->server.start();

  const auto* cat = state_->backend.catalog;
  RunningBackend rb;
  rb.port = state_->port;
  rb.pid = platform::current_pid();
  rb.engine = state_->backend.engine;
  rb.domain = cat ? cat->domain : "";
  rb.cwd = std::filesystem::current_path();
  rb.backend_dir = cat ? cat->dir : std::filesystem::path{};
  rb.executable = "";
  rb.ws_url = "ws://localhost:" + std::to_string(state_->port);
  rb.started_at = 0;
  BackendManager::register_running_backend(rb);

  set_restart_handler([this] {
    BackendManager::unregister_running_backend(state_->port);
    state_->server.stop();
    exec_remembered_args();
  });
  return {};
}

void Server::stop() {
  BackendManager::unregister_running_backend(state_->port);
  set_restart_handler(nullptr);
  state_->server.stop();
  std::lock_guard lock(state_->mutex);
  state_->sessions.clear();
}

int serve(const Backend& backend, int port) {
  Server server(backend, port);
  if (auto error = server.start(); !error.empty()) {
    std::cerr << "cannot listen on port " << port << ": " << error << '\n';
    return 1;
  }
  std::cout << "paperboat backend (" << backend.engine << ") on ws://localhost:" << port << std::endl;
  for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}

}  // namespace paperboat::core
