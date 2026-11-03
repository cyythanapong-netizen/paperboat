// A backend's command line: serve the protocol, or run the starter once (`--run`) for a
// test that checks a backend's result without a UI.

#include <iostream>
#include <stdexcept>
#include <string>

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
    } else {
      throw std::invalid_argument("unknown option " + arg + " (use --port N or --run)");
    }
  }
  return options;
}

std::string run_starter(const Backend& backend, std::ostream& out) {
  Backend headless = backend;
  headless.run.real_time = false;
  const auto& catalog = *backend.catalog;
  const json station = catalog.station.empty() ? json() : json::parse(catalog.station);
  const auto emit = [&out](const json& msg) { out << msg.dump() << '\n'; };
  const auto status = run("run-1", json::parse(catalog.graph), station, headless, emit, nullptr, {});
  out.flush();  // a host may end the process right after (the Python bindings do)
  return status;
}

int main(const Backend& backend, int argc, char** argv, int default_port) {
  Options options;
  try {
    options = parse_options(argc, argv, default_port);
  } catch (const std::exception& err) {
    std::cerr << err.what() << '\n';
    return 2;
  }
  if (options.run) return run_starter(backend, std::cout) == "done" ? 0 : 1;
  return serve(backend, options.port);
}

}  // namespace paperboat::core
