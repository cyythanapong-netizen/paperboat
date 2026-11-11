#pragma once

// The server that speaks the protocol for a Backend (run.hpp). A backend's main() fills a
// Backend and returns paperboat::core::main(backend, argc, argv, port); see GUIDELINES §6.

#include <filesystem>
#include <functional>
#include <ostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "paperboat/core/run.hpp"

namespace paperboat::core {

// One connection's protocol state. Thread-safe: messages arrive on the socket's thread,
// runs execute on their own. `send` must be safe to call from any thread.
class Session {
 public:
  using Send = std::function<void(const std::string& text)>;
  using Remote = std::function<void(const json& variables)>;
  Session(const Backend& backend, Send send, Remote remote = {});  // sends the handshake
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  void on_message(const std::string& text);
  /** Send a server message to this connection (the server's shared variables use it). */
  void send_message(const json& message);

 private:
  struct State;
  std::unique_ptr<State> state_;
};

json load_json(const std::filesystem::path& path);

// A backend's command line: `--port N` (serve there) and `--run` (run the starter graph on
// the starter station once, headless and as fast as it can, print every protocol message
// as one JSON line, and exit: 0 when the run is done, 1 when it failed). `--var name=value`
// (any number of them) sets a default variable (#36) for that run.
struct Options {
  int port = 0;
  bool run = false;
  bool catalog = false;  // --catalog: print the catalog's block and station object types, then exit
  std::vector<std::pair<std::string, std::string>> vars = {};
  std::optional<double> duration = std::nullopt;
  std::string project = "";
};
Options parse_options(int argc, char** argv, int default_port);  // throws std::invalid_argument

// The starter run of `--run`: every message to `out`, one JSON line each, with `vars` set
// over the descriptor's default variables. Returns the run's final status ("done",
// "failed"). Throws std::invalid_argument for a --var the backend doesn't have, or a value
// that isn't of its type.
std::string run_starter(const Backend& backend, std::ostream& out,
                        const std::vector<std::pair<std::string, std::string>>& vars = {},
                        std::optional<double> duration = std::nullopt);

// The descriptor's default variables with `vars` (name, text) set over them, each value
// read as its variable's type. Throws std::invalid_argument.
json with_vars(const toolkit::Catalog& catalog, const std::vector<std::pair<std::string, std::string>>& vars);

// What a backend's main() returns: parse the options, then serve or run the starter.
int main(const Backend& backend, int argc, char** argv, int default_port);

// Remember the process command line so `backend.restart` can relaunch the same command.
void remember_args(int argc, char** argv);
void remember_args(std::vector<std::string> args);
void set_restart_handler(std::function<void()> on_restart);
void exec_remembered_args();
bool restart_process();

// `--port N` from the command line, or `fallback` (parse_options reads every option).
int port_arg(int argc, char** argv, int fallback);

// Serve the protocol on ws://127.0.0.1:port until the process ends. Returns non-zero if
// it can't listen.
int serve(const Backend& backend, int port);

// Serves a built web app's folder (index.html, assets/) over HTTP until killed: an installed
// paperboat's `start` with no checkout (share/paperboat/web). Unknown paths get index.html, as a
// single-page app wants. Returns 1 when the port can't be taken.
int serve_files(const std::filesystem::path& dir, int port, const std::string& host);

// The same server, started and stopped by the caller (a host such as the Python
// bindings, which must keep its own main loop for Ctrl+C).
class Server {
 public:
  Server(const Backend& backend, int port);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  std::string start();  // "" when listening, else why not
  void stop();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace paperboat::core
