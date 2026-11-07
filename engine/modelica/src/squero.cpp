// The transport for squero/0: the only file in engine/modelica that knows sockets.

#include "paperboat/modelica/squero.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>

namespace paperboat::modelica {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

std::string status(OmcState state, const std::string& version) {
  return json{{"op", "status"}, {"state", name(state)}, {"version", version}}.dump();
}

ix::HttpResponsePtr respond(int code, const std::string& type, std::string body) {
  ix::WebSocketHttpHeaders headers;
  headers["Content-Type"] = type;
  headers["Cache-Control"] = "no-cache";
  return std::make_shared<ix::HttpResponse>(code, code == 200 ? "OK" : "Not Found", ix::HttpErrorCode::Ok, headers,
                                            std::move(body));
}

// The built app's files, only from inside `app`; `/` is index.html.
ix::HttpResponsePtr http(const fs::path& app, const ix::HttpRequestPtr& request) {
  if (app.empty()) return respond(404, "text/plain", "squero/0: connect over WebSocket\n");
  std::string path = request->uri.substr(0, request->uri.find('?'));
  if (path == "/") path = "/index.html";
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(app, ec);
  const fs::path file = fs::weakly_canonical(app / path.substr(1), ec);
  const auto inside = file.lexically_relative(root);
  if (ec || inside.empty() || *inside.begin() == ".." || !fs::is_regular_file(file))
    return respond(404, "text/plain", "not found\n");
  static const std::map<std::string, std::string> types = {
      {".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
      {".css", "text/css; charset=utf-8"}, {".svg", "image/svg+xml"}, {".json", "application/json"},
      {".woff2", "font/woff2"}, {".png", "image/png"}};
  const auto type = types.find(file.extension().string());
  std::ifstream in(file, std::ios::binary);
  std::stringstream body;
  body << in.rdbuf();
  return respond(200, type == types.end() ? "application/octet-stream" : type->second, body.str());
}

}  // namespace

std::string handle(Omc& omc, const std::string& message) {
  json in;
  try {
    in = json::parse(message);
  } catch (const json::exception&) {
    return json{{"ok", false}, {"error", "not JSON"}}.dump();
  }
  json out{{"id", in.value("id", json())}};
  if (in.value("op", "") != "omc" || !in.contains("expr") || !in["expr"].is_string()) {
    out["ok"] = false;
    out["error"] = "unknown op; squero/0 has {op: \"omc\", expr}";
    return out.dump();
  }
  const std::string expr = in["expr"];
  if (!allowed(expr)) {
    out["ok"] = false;
    out["error"] = "not allowed: " + expr.substr(0, expr.find('(')) + " (Studio may call only the squero/0 allow-list)";
    return out.dump();
  }
  try {
    out["result"] = omc.call(expr);
    out["ok"] = true;
  } catch (const std::exception& e) {
    out["ok"] = false;
    out["error"] = e.what();
  }
  return out.dump();
}

int serve_squero(int port, const fs::path& app, Omc::Options options) {
  ix::initNetSystem();
  Omc omc(std::move(options));
  ix::HttpServer server(port, "127.0.0.1");
  std::mutex mutex;
  std::set<ix::WebSocket*> clients;

  omc.on_status([&](OmcState state, const std::string& version) {
    const std::string text = status(state, version);
    std::lock_guard lock(mutex);
    for (auto* ws : clients) ws->sendText(text);
  });
  server.setOnConnectionCallback(
      [&app](const ix::HttpRequestPtr& request, const std::shared_ptr<ix::ConnectionState>&) { return http(app, request); });
  server.setOnClientMessageCallback(
      [&](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
          {
            std::lock_guard lock(mutex);
            clients.insert(&ws);
          }
          ws.sendText(status(omc.state(), omc.version()));
        } else if (msg->type == ix::WebSocketMessageType::Close) {
          std::lock_guard lock(mutex);
          clients.erase(&ws);
        } else if (msg->type == ix::WebSocketMessageType::Message) {
          // Each connection has its own callback thread; omc serialises the calls.
          if (std::string reply = handle(omc, msg->str); !reply.empty()) ws.sendText(reply);
        }
      });
  if (auto [ok, error] = server.listen(); !ok) {
    std::cerr << "cannot listen on port " << port << ": " << error << '\n';
    return 1;
  }
  server.start();
  std::cout << "squero studio OM server (squero/0) on ws://localhost:" << port
            << (app.empty() ? std::string() : "  app: http://localhost:" + std::to_string(port) + "/") << std::endl;
  // Start omc now so the first status a client sees is real.
  try {
    omc.call("getVersion()");
  } catch (const std::exception& e) {
    std::cerr << "omc: " << e.what() << '\n';
  }
  for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}

}  // namespace paperboat::modelica
