// A backend's command line: serve the protocol, or run the starter once (`--run`) for a
// test that checks a backend's result without a UI.

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "paperboat/core/backend.hpp"

namespace paperboat::core {

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

namespace {
int g_argc = 0;
char** g_argv = nullptr;
}  // namespace

void remember_args(int argc, char** argv) {
  g_argc = argc;
  g_argv = argv;
}

void restart_backend() {
  if (g_argv == nullptr) return;
  const pid_t child = fork();
  if (child == 0) {
    // The inherited listening socket would keep the port busy; drop every descriptor
    // except stdin/stdout/stderr before becoming the new backend.
    for (int fd = 3; fd < 1024; ++fd) close(fd);
    // Give the parent time to release the listening socket.
    sleep(2);
    execv(g_argv[0], g_argv);
    _exit(127);
  }
  if (child > 0) {
    _exit(0);
  }
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
