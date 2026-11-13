// A backend's command line: serve the protocol, or run the starter once (`--run`) for a
// test that checks a backend's result without a UI.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "paperboat/core/backend.hpp"
#include "paperboat/core/record.hpp"

namespace paperboat::core {
namespace {

std::mutex g_restart_mutex;
std::vector<std::string> g_restart_args;
std::function<void()> g_restart_handler;

}  // namespace

void remember_args(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(std::max(argc, 0)));
  for (int i = 0; i < argc; ++i) {
    if (argv[i]) args.emplace_back(argv[i]);
  }
  remember_args(std::move(args));
}

void remember_args(std::vector<std::string> args) {
  std::lock_guard lock(g_restart_mutex);
  g_restart_args = std::move(args);
}

void set_restart_handler(std::function<void()> on_restart) {
  std::lock_guard lock(g_restart_mutex);
  g_restart_handler = std::move(on_restart);
}

void exec_remembered_args() {
  std::vector<std::string> args;
  {
    std::lock_guard lock(g_restart_mutex);
    args = g_restart_args;
  }
  if (args.empty()) return;
#if defined(_WIN32)
  // Windows has no exec: start the same program again, detached, and end this one, so the binary
  // that serves the port next is the one just rebuilt (a toolkit install asks for this).
  std::string command;
  for (const auto& arg : args) {
    if (!command.empty()) command += ' ';
    command += '"';
    for (char c : arg) {
      if (c == '"') command += '\\';
      command += c;
    }
    command += '"';
  }
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  std::exit(0);
#else
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  execvp(argv[0], argv.data());
#endif
}

bool restart_process() {
  std::function<void()> handler;
  bool has_args = false;
  {
    std::lock_guard lock(g_restart_mutex);
    handler = g_restart_handler;
    has_args = !g_restart_args.empty();
  }
  if (handler) {
    handler();
    return true;
  }
  if (!has_args) return false;
  exec_remembered_args();
  return false;
}

Options parse_options(int argc, char** argv, int default_port) {
  Options options{.port = default_port, .run = false};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--run") {
      options.run = true;
    } else if (arg == "--catalog") {
      options.catalog = true;
    } else if (arg == "--port" && i + 1 < argc) {
      options.port = std::stoi(argv[++i]);
    } else if (arg == "--var" && i + 1 < argc) {
      const std::string setting = argv[++i];
      const auto eq = setting.find('=');
      if (eq == std::string::npos || eq == 0) throw std::invalid_argument("--var takes name=value, not " + setting);
      options.vars.emplace_back(setting.substr(0, eq), setting.substr(eq + 1));
    } else if (arg == "--duration" && i + 1 < argc) {
      options.duration = std::stod(argv[++i]);
    } else if (arg == "--project" && i + 1 < argc) {
      options.project = argv[++i];
    } else {
      throw std::invalid_argument("unknown option " + arg + " (use --port N, --run, --catalog, --duration S, --project P, --var name=value)");
    }
  }
  return options;
}

json with_vars(const toolkit::Catalog& catalog, const std::vector<std::pair<std::string, std::string>>& vars) {
  json list = catalog.variables.empty() ? json::array() : json::parse(catalog.variables);
  for (const auto& [name, text] : vars) {
    auto it = std::ranges::find_if(list, [&](const json& v) { return v.at("name") == name; });
    if (it == list.end()) {
      std::string known;
      for (const auto& v : list) known += (known.empty() ? "" : ", ") + v.at("name").get<std::string>();
      throw std::invalid_argument("--var " + name + ": no such variable" + (known.empty() ? "" : " (this backend has " + known + ")"));
    }
    const auto type = it->at("type").get<std::string>();
    json value = text;
    if (type == "number") {
      std::size_t used = 0;
      double x = 0;
      try {
        x = std::stod(text, &used);
      } catch (const std::exception&) {
        used = 0;
      }
      if (used == 0 || used != text.size()) throw std::invalid_argument("--var " + name + ": a number, not \"" + text + "\"");
      value = x;
    } else if (type == "boolean") {
      if (text != "true" && text != "false") throw std::invalid_argument("--var " + name + ": true or false, not \"" + text + "\"");
      value = text == "true";
    }
    it->erase("expr");  // a formula's value set by hand
    (*it)["value"] = std::move(value);
  }
  return list;
}

std::string run_starter(const Backend& backend, std::ostream& out, const std::vector<std::pair<std::string, std::string>>& vars,
                        std::optional<double> duration) {
  Backend headless = backend;
  headless.run.real_time = false;
  if (duration) headless.run.duration = *duration;
  const auto& catalog = *backend.catalog;
  if (!catalog.missing_imports.empty()) {
    std::string missing;
    for (const auto& m : catalog.missing_imports) missing += (missing.empty() ? "" : ", ") + m;
    out << json({{"type", "log"}, {"level", "error"}, {"text", "cannot run starter: missing required toolkits: " + missing}}).dump() << '\n';
    out << json({{"type", "run.finished"}, {"run", "run-1"}, {"status", "failed"}}).dump() << '\n';
    out.flush();
    return "failed";
  }
  json graph = json::parse(catalog.graph);
  json station = catalog.station.empty() ? json() : json::parse(catalog.station);
  json variables = with_vars(catalog, vars);
  if (!backend.initial_project.is_null() && backend.initial_project.is_object()) {
    const auto& p = backend.initial_project;
    if (p.contains("flow") && p["flow"].is_object()) graph = p["flow"];
    if (p.contains("station")) station = p["station"];
    if (p.contains("variables") && p["variables"].is_array()) variables = p["variables"];
    if (p.contains("run") && p["run"].is_object()) headless.run = with_overrides(headless.run, p["run"]);
  }
  // A headless record (#277), when `paperboat run/open --record` asked for one.
  std::optional<RunRecord> record;
  if (auto meta = record_meta_from_env()) {
    record.emplace(std::move(*meta), backend.engine,
                   json{{"graph", graph}, {"station", station}, {"variables", variables}, {"settings", settings_json(headless.run)}});
  }
  const auto emit = [&out, &record](const json& msg) {
    if (record) {
      try {
        record->add(msg);
      } catch (const std::exception& err) {
        std::cerr << "error: cannot write run record: " << err.what() << '\n';
        record.reset();
      }
    }
    out << msg.dump() << '\n';
  };
  const auto status = run("run-1", graph, station, headless, emit, nullptr, {}, nullptr, variables);
  out.flush();  // a host may end the process right after (the Python bindings do)
  return status;
}

int main(const Backend& backend, int argc, char** argv, int default_port) {
  remember_args(argc, argv);
  Options options;
  try {
    options = parse_options(argc, argv, default_port);
  } catch (const std::exception& err) {
    std::cerr << err.what() << '\n';
    return 2;
  }
  Backend b = backend;
  if (const char* run = std::getenv("PAPERBOAT_RUN"); run && *run) {  // backend.toml's [run], from paperboat
    try {
      b.run = with_overrides(b.run, json::parse(run));
    } catch (const std::exception& err) {
      std::cerr << "error: PAPERBOAT_RUN (backend.toml [run]): " << err.what() << '\n';
      return 2;
    }
  }
  if (!options.project.empty()) {
    try {
      b.initial_project = load_json(options.project);
      b.initial_project_name = std::filesystem::path(options.project).stem().string();
      if (b.initial_project_name.ends_with(".paperboat")) {
        b.initial_project_name = b.initial_project_name.substr(0, b.initial_project_name.size() - 10);
      }
    } catch (const std::exception& err) {
      std::cerr << "error: cannot load project file " << options.project << ": " << err.what() << '\n';
      return 1;
    }
  }
  if (options.catalog) {
    // What the backend really offers, with the blocks and station objects its code registered
    json blocks = json::array(), objects = json::array();
    for (const auto& e : b.catalog->entries) blocks.push_back(e.def.type);
    for (const auto& o : b.catalog->objects) objects.push_back(o.def.type);
    std::cout << json{{"blocks", blocks}, {"objects", objects}}.dump() << '\n';
    return 0;
  }
  if (options.run) {
    try {
      return run_starter(b, std::cout, options.vars, options.duration) == "done" ? 0 : 1;
    } catch (const std::invalid_argument& err) {
      std::cerr << err.what() << '\n';
      return 2;
    }
  }
  return serve(b, options.port);
}

}  // namespace paperboat::core
