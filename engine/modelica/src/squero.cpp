// The transport for squero/0: the only file in engine/modelica that knows sockets.

#include "paperboat/modelica/squero.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

#include "paperboat/modelica/generate.hpp"
#include "paperboat/modelica/import.hpp"

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


// --- Studio's own ops ---

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  return s.substr(a);
}

std::string text_of(const std::string& omc_answer) { return parse_value(omc_answer).atom; }

bool is_msl(const std::string& model) { return model == "Modelica" || model.starts_with("Modelica."); }

// A user model, never the library: Studio edits only the user's file.
std::string user_model(const json& in) {
  std::string model = in.value("model", "");
  if (model.empty() || is_msl(model) || model.find_first_of(" ;(){}\"") != std::string::npos)
    throw std::runtime_error("not a model Studio may edit: " + model);
  return model;
}

void ensure_library(Omc& omc, const std::string& cls) {
  const std::string top = cls.substr(0, cls.find('.'));
  for (const auto& v : parse_value(omc.call("getClassNames()")).items)
    if (v.atom == top) return;
  if (top == "Modelica") omc.call("loadModel(Modelica)");
}

// The class `file` defines, once loaded.
std::string load_file(Omc& omc, const fs::path& file) {
  ensure_library(omc, "Modelica");
  if (omc.call("loadFile(" + quote(fs::absolute(file).string()) + ")") != "true")
    throw std::runtime_error("cannot load " + file.string() + ": " + omc.errors());
  std::error_code ec;
  const fs::path want = fs::weakly_canonical(file, ec);
  for (const auto& v : parse_value(omc.call("getClassNames()")).items) {
    if (v.atom == "Modelica") continue;
    const std::string source = text_of(omc.call("getSourceFile(" + v.atom + ")"));
    if (!source.empty() && fs::weakly_canonical(source, ec) == want) return v.atom;
  }
  throw std::runtime_error("no class in " + file.string());
}

json op_open(Omc& omc, const Session& session) {
  fs::path file = session.file.empty() ? fs::path("Untitled.mo") : session.file;
  if (!fs::exists(file)) {
    const std::string name = file.stem().string();
    std::ofstream(file) << "model " << name << "\nend " << name << ";\n";
  }
  return {{"file", fs::absolute(file).string()}, {"model", load_file(omc, file)}};
}

json op_save(Omc& omc, const json& in) {
  const std::string model = user_model(in);
  const std::string file = text_of(omc.call("getSourceFile(" + model + ")"));
  if (file.empty()) throw std::runtime_error(model + " has no file to save to");
  if (omc.call("save(" + model + ")") != "true") throw std::runtime_error("cannot save " + model + ": " + omc.errors());
  return {{"file", file}};
}

json op_icon(Omc& omc, const json& in) {
  const std::string cls = in.value("class", "");
  if (cls.empty() || cls.find_first_of(" ;(){}\"") != std::string::npos) throw std::runtime_error("icon needs a class name");
  static std::mutex cache_mutex;
  static std::map<std::string, json> cache;  // library classes never change under Studio
  if (is_msl(cls)) {
    std::lock_guard lock(cache_mutex);
    if (const auto it = cache.find(cls); it != cache.end()) return it->second;
  }
  ensure_library(omc, cls);
  json out = import_class(omc, cls).to_json();
  if (is_msl(cls)) {
    std::lock_guard lock(cache_mutex);
    cache[cls] = out;
  }
  return out;
}

json op_set_icon(Omc& omc, const json& in) {
  const std::string model = user_model(in);
  if (!in.contains("icon") || !in["icon"].is_object()) throw std::runtime_error("set_icon needs an icon");
  const std::string icon = icon_annotation(in["icon"]);
  if (omc.call("addClassAnnotation(" + model + ", annotate=" + icon + ")") != "true")
    throw std::runtime_error("cannot set the icon: " + omc.errors());
  return json::object();
}

json op_generate(Omc& omc, const json& in, const Session& session) {
  GenerateOptions o{.source = in.value("source", ""), .model = in.value("model", ""), .out = in.value("out", ""),
                    .path = in.value("path", "auto"), .dry = in.value("dry", false)};
  if (o.source.empty()) throw std::runtime_error("generate needs a source .mo");
  if (!o.dry && o.out.empty()) throw std::runtime_error("generate needs an out folder");
  const json id = in.value("id", json());
  if (session.push)
    o.progress = [&](std::string_view stage, double fraction) {
      session.push(json{{"op", "progress"}, {"id", id}, {"stage", stage}, {"fraction", fraction}}.dump());
    };
  const GenerateResult r = generate(omc, o);
  json out{{"model", r.model}, {"path", r.path}, {"reason", r.reason}, {"out", r.out.string()}, {"notices", r.notices}};
  if (o.dry) out["files"] = r.files;
  return out;
}

// One CSV row of numbers; NaN and infinity become null (JSON has neither).
std::vector<json> numbers(const std::string& line) {
  std::vector<json> out;
  std::stringstream row(line);
  for (std::string cell; std::getline(row, cell, ',');) {
    const double v = std::strtod(cell.c_str(), nullptr);
    out.push_back(std::isfinite(v) ? json(v) : json());
  }
  return out;
}

json op_simulate(Omc& omc, const json& in) {
  const std::string model = user_model(in);
  const double stop = in.value("stop", 1.0);
  if (!(stop > 0.0) || !std::isfinite(stop)) throw std::runtime_error("stop time must be above 0");
  std::string filter;
  for (const auto& v : in.value("vars", json::array())) {
    const std::string name = v.get<std::string>();
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.[](),") != std::string::npos)
      throw std::runtime_error("not a variable name: " + name);
    std::string re;
    for (const char c : name) re += (std::string_view(".[]()").find(c) != std::string_view::npos ? "\\" : "") + std::string(1, c);
    filter += (filter.empty() ? "" : "|") + re;
  }
  std::string expr = "simulate(" + model + ", startTime=0, stopTime=" + std::to_string(stop) +
                     ", numberOfIntervals=500, outputFormat=\"csv\"";
  if (!filter.empty()) expr += ", variableFilter=" + quote(filter);
  const std::string answer = omc.call(expr + ")", std::chrono::minutes(5));
  const auto at = answer.find("resultFile = \"");
  std::string file;
  if (at != std::string::npos) file = answer.substr(at + 14, answer.find('"', at + 14) - at - 14);
  std::ifstream csv(file);
  std::string header;
  if (file.empty() || !std::getline(csv, header)) {
    const auto m = answer.find("messages = \"");
    std::string why = m == std::string::npos ? "" : answer.substr(m + 12, answer.find("\",\n", m) - m - 12);
    throw std::runtime_error("simulation failed: " + trim(why.empty() ? omc.errors() : why));
  }
  json names = json::array();
  std::vector<std::vector<json>> columns;
  {
    std::stringstream row(header);
    for (std::string cell; std::getline(row, cell, ',');) {
      if (cell.size() >= 2 && cell.front() == '"') cell = cell.substr(1, cell.size() - 2);
      names.push_back(cell);
    }
  }
  columns.resize(names.size());
  for (std::string line; std::getline(csv, line);) {
    const auto row = numbers(line);
    for (std::size_t i = 0; i < columns.size() && i < row.size(); ++i) columns[i].push_back(row[i]);
  }
  json series = json::object();
  json all = json::array();
  for (std::size_t i = 1; i < names.size(); ++i) {
    series[names[i].get<std::string>()] = columns[i];
    all.push_back(names[i]);
  }
  return {{"t", columns.empty() ? std::vector<json>{} : columns[0]}, {"series", series}, {"names", all}};
}

}  // namespace

std::string handle(Omc& omc, const std::string& message, const Session& session) {
  json in;
  try {
    in = json::parse(message);
  } catch (const json::exception&) {
    return json{{"ok", false}, {"error", "not JSON"}}.dump();
  }
  json out{{"id", in.value("id", json())}};
  const std::string op = in.value("op", "");
  try {
    if (op == "omc" && in.contains("expr") && in["expr"].is_string()) {
      const std::string expr = in["expr"];
      if (!allowed(expr)) {
        out["ok"] = false;
        out["error"] = "not allowed: " + expr.substr(0, expr.find('(')) + " (Studio may call only the squero/0 allow-list)";
        return out.dump();
      }
      out["result"] = omc.call(expr);
    } else if (op == "open") {
      out["result"] = op_open(omc, session);
    } else if (op == "save") {
      out["result"] = op_save(omc, in);
    } else if (op == "icon") {
      out["result"] = op_icon(omc, in);
    } else if (op == "set_icon") {
      out["result"] = op_set_icon(omc, in);
    } else if (op == "generate") {
      out["result"] = op_generate(omc, in, session);
    } else if (op == "simulate") {
      out["result"] = op_simulate(omc, in);
    } else {
      out["ok"] = false;
      out["error"] = "unknown op; squero/0 has omc, open, save, icon, set_icon, generate, simulate";
      return out.dump();
    }
    out["ok"] = true;
  } catch (const std::exception& e) {
    out["ok"] = false;
    out["error"] = e.what();
  }
  return out.dump();
}

int serve_squero(int port, const fs::path& app, Omc::Options options, const fs::path& file) {
  ix::initNetSystem();
  Omc omc(std::move(options));
  ix::HttpServer server(port, "127.0.0.1");
  // Recursive: sending to a client that has just gone away runs its Close handler on this same
  // thread, which takes the lock again. The send loop walks a copy for the same reason.
  std::recursive_mutex mutex;
  std::set<ix::WebSocket*> clients;

  omc.on_status([&](OmcState state, const std::string& version) {
    const std::string text = status(state, version);
    std::lock_guard lock(mutex);
    for (auto* ws : std::set<ix::WebSocket*>(clients)) ws->sendText(text);
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
          const Session session{[&ws](const std::string& text) { ws.sendText(text); }, file};
          if (std::string reply = handle(omc, msg->str, session); !reply.empty()) ws.sendText(reply);
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
