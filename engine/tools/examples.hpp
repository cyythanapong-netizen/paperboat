// The example backends: a folder of backend folders, each with its own backend.toml (name, language,
// port). Only the CLI knows they exist; the engine never names them.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace paperboat::cli {

struct Example {
  std::string name;
  std::string language;  // "cpp" or "python"
  int port = 0;
  std::filesystem::path dir;
  bool running = false;
  int pid = 0;
};

// The examples folder: `hint`, else $PAPERBOAT_EXAMPLES_DIR, else a ../paperboat-examples checkout,
// else the copy `paperboat pkg install paperboat-examples` made. Empty if none has a backend in it.
std::filesystem::path find_examples_dir(const std::filesystem::path& hint = {});

// Every backend folder (one with a backend.toml) in `dir`, by port.
std::vector<Example> list_examples(const std::filesystem::path& dir);

bool start_examples(const std::filesystem::path& dir);
bool stop_examples(const std::filesystem::path& dir);

}  // namespace paperboat::cli
