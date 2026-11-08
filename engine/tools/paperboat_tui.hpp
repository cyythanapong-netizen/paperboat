#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace paperboat::tui {

// The CLI's version, from PAPERBOAT_VERSION (set by engine/tools/CMakeLists.txt; "0.0.0-dev" if unset).
extern const char* const kVersion;

// The git tag, or "branch @ hash", or the hash, of the working directory; else "release <version>".
std::string get_git_info();

// The current directory with the home directory shortened to ~.
std::string get_compact_cwd();

// Prints the brand boat and version; with `animated` and on a terminal, ripples the water.
void render_banner(bool animated = true);

/**
 * Reusable terminal spinner utilizing the Paperboat 5-dot water ripple.
 * Animates the wave crest across the dots in signal orange while a task runs.
 */
class WaveSpinner {
 public:
  explicit WaveSpinner(std::string message = "") : message_(std::move(message)) {}
  ~WaveSpinner() { stop(); }

  void start(std::string msg = "");
  void stop(bool ok = true, std::string_view final_msg = "");

  void set_message(std::string msg) { message_ = std::move(msg); }

 private:
  std::string message_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// Splits `line` on whitespace; single or double quotes group a token.
std::vector<std::string> parse_command_line(std::string_view line);

}  // namespace paperboat::tui
