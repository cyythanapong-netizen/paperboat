// A backend's command line: serve the protocol, or run the starter once (`--run`) for a
// test that checks a backend's result without a UI.

#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "paperboat/core/backend.hpp"

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
#if !defined(_WIN32)
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
    } else if (arg == "--port" && i + 1 < argc) {
      options.port = std::stoi(argv[++i]);
    } else if (arg == "--var" && i + 1 < argc) {
      const std::string setting = argv[++i];
      const auto eq = setting.find('=');
      if (eq == std::string::npos || eq == 0) throw std::invalid_argument("--var takes name=value, not " + setting);
      options.vars.emplace_back(setting.substr(0, eq), setting.substr(eq + 1));
    } else {
      throw std::invalid_argument("unknown option " + arg + " (use --port N, --run, --var name=value)");
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

std::string run_starter(const Backend& backend, std::ostream& out, const std::vector<std::pair<std::string, std::string>>& vars) {
  Backend headless = backend;
  headless.run.real_time = false;
  const auto& catalog = *backend.catalog;
  const json station = catalog.station.empty() ? json() : json::parse(catalog.station);
  const json variables = with_vars(catalog, vars);
  const auto emit = [&out](const json& msg) { out << msg.dump() << '\n'; };
  const auto status = run("run-1", json::parse(catalog.graph), station, headless, emit, nullptr, {}, nullptr, variables);
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
  if (options.run) {
    try {
      return run_starter(backend, std::cout, options.vars) == "done" ? 0 : 1;
    } catch (const std::invalid_argument& err) {
      std::cerr << err.what() << '\n';
      return 2;
    }
  }
  return serve(backend, options.port);
}

}  // namespace paperboat::core
