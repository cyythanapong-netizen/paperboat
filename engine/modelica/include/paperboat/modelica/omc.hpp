#pragma once

// The OpenModelica compiler as a long-lived process (#160 contracts §5): one
// `omc --interactive=zmq -z=<session>` per Omc, spoken to over ZeroMQ REQ/REP. One request
// in flight at a time; a request that times out or finds omc dead restarts it (replaying the
// libraries and files it had loaded), and after 3 restarts in a row the bridge is `failed`.
//
// libzmq is opened at run time (libzmq.so.5, which omc itself ships), so building this needs
// no ZeroMQ headers; only running it needs omc.

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace paperboat::modelica {

struct OmcError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

enum class OmcState { Ready, Checking, Failed };
std::string_view name(OmcState state);  // "ready", "checking", "failed"

class Omc {
 public:
  struct Options {
    std::string omc = "omc";                          // the executable, on PATH or a path
    std::chrono::milliseconds timeout{30'000};        // per request
    int max_restarts = 3;                             // in a row, then failed
  };
  using StatusFn = std::function<void(OmcState, const std::string& version)>;

  Omc();
  explicit Omc(Options options);
  ~Omc();
  Omc(const Omc&) = delete;
  Omc& operator=(const Omc&) = delete;

  // Evaluates one omc expression, e.g. `checkModel(M)`, and returns omc's answer as text
  // (`true`, `"…"`, `{…}`), without the trailing newline. Starts omc on first use. Throws
  // OmcError when omc can't start, or failed (OmcState::Failed).
  std::string call(std::string_view expr);
  std::string call(std::string_view expr, std::chrono::milliseconds timeout);

  // `getErrorString()` unquoted: what omc said about the calls since the last one.
  std::string errors();

  OmcState state() const;
  std::string version() const;  // "OpenModelica 1.27…" once started, else ""
  // Called (from the calling thread) on every state change.
  void on_status(StatusFn fn);

  // The folder omc runs in (its build output and dumps land here).
  std::string workdir() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Whether `expr` may come from a Studio client over squero/0: one call to a function on the
// allow-list (contracts §5), whose nested calls are allow-listed too or are annotation
// records (capitalised: Placement(…), Line(…)) or their `transformation`s. No `;`, no `:=`.
bool allowed(std::string_view expr);
const std::vector<std::string>& allowed_calls();

// omc's answers, parsed: `{a, "b", {c}}` → a list of atoms and lists. Strings keep their
// unescaped text; other atoms their spelling (`true`, `1.5`, `Modelica.Blocks`).
struct OmValue {
  bool is_list = false;
  bool is_string = false;
  std::string atom;
  std::vector<OmValue> items;

  const OmValue& operator[](std::size_t i) const { return items.at(i); }
  std::size_t size() const { return items.size(); }
};
OmValue parse_value(std::string_view text);  // throws OmcError on malformed text
std::string quote(std::string_view text);    // a Modelica string literal

}  // namespace paperboat::modelica
