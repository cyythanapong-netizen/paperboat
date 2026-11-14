#pragma once

// A run: a graph on a station and the domain's state. Knows nothing of sockets: it reports
// through `emit`, so a session sends the events and a test collects them.
//
// Run styles (Backend::run.mode), so a backend needn't be a simulation:
// - Clock: step everything on a clock (a Signal flow, or a Step flow if the graph has step
//   blocks), streaming signals and scene patches.
// - Once: evaluate a Signal flow a single time (a build, a query, a geometry), then show
//   the result. Wires may carry lists, tables and text. Given a cache, a run reuses the
//   outputs of blocks that nothing changed for since the last run (cache.hpp).

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "paperboat/core/graph.hpp"
#include "paperboat/toolkit/solve.hpp"

namespace paperboat::core {

struct RunSettings {
  enum class Mode { Clock, Once };
  Mode mode = Mode::Clock;
  double dt = 0.01;        // clock step, s
  double duration = 10.0;  // run length (a Step flow: its limit), s
  int emit_every = 4;      // send signals and scene patches every N steps
  bool real_time = true;   // pace emits at wall-clock speed (tests turn it off)
  double speed = 1.0;      // times real time when paced; 0: as fast as it can (a run.speed may change it, #219)
  // The run's solver and its options: what a block or object whose `solver` is "auto" uses
  // ("rk4", "rk45", "rk2", "euler", "discrete", "cvode", "ida": solve::names()).
  std::string solver = "rk4";
  toolkit::solve::Options solver_options = {};
};

// `base` with a graph.run's `settings` on top (duration, dt, speed, solver, solver_options:
// the ones given, over base's; never mode). Throws std::invalid_argument for a value out of range.
RunSettings with_overrides(RunSettings base, const json& settings);
// As hello.run announces them: mode, duration, dt, speed, solver, solver_options.
json settings_json(const RunSettings& settings);

class Executor;  // executor.hpp: how a graph runs
class RunCache;  // cache.hpp: outputs a run-once keeps for the next

// What a backend is made of. A simple backend fills one and calls serve() (backend.hpp).
struct Backend {
  std::string engine;                         // the name in `hello`
  const toolkit::Catalog* catalog = nullptr;  // with its starters; outlives the backend
  // A fresh domain state per run (and one for the opening scene); empty if none.
  std::function<std::unique_ptr<toolkit::DomainState>()> state = {};
  json view = nullptr;                        // the scene's camera hint ({"mode": "2d", …})
  RunSettings run = {};
  // Its own run style, if the core's three (clock, once, steps) don't fit: an executor for
  // this graph. Empty: default_executor().
  std::function<std::unique_ptr<Executor>(const Graph&, const RunSettings&)> executor = {};
  json initial_project = nullptr;
  std::string initial_project_name = "";
};

// analysis.request (pb#370): the part of the graph it names, made as a run makes it, given to
// the catalog's analysis of that name. Returns the analysis.result message: its result, or the
// error that stopped it. `station` and `variables`: the session's, unless the request has its own.
json analyse(const json& request, const Backend& backend, const json& station, const json& variables);

using Emit = std::function<void(const json& message)>;
using KeysNow = std::function<toolkit::KeySet()>;  // keys held down in the UI right now
// The variables list the UI last sent, if it sent one since the last call (#75); read between steps.
using VariablesNow = std::function<std::optional<json>()>;
// The speed the UI last asked for (run.speed, #219), if it asked since the last call; read between frames.
using SpeedNow = std::function<std::optional<double>()>;

// A run.speed's speed, checked: a finite number, 0 or more (0: as fast as it can). Throws
// std::invalid_argument for anything else.
double checked_speed(const json& speed);

// The scene: station objects, the domain state's entities and what blocks show.
json scene_message(const Station& station, const toolkit::DomainState* state, const json& view,
                   const std::vector<const toolkit::Block*>& blocks = {});

// Emits run.started, then scene, signal, scene.patch, node.state, run.progress, run.speed and
// log events, then run.finished. Returns the final status: "done", "failed" or "cancelled".
// `settings`: graph.run's overrides of the backend's run settings, if any. `variables`:
// the variables (#36) the graph's and station's references resolve against. `tune`: where
// the run looks for a changed variables list while it runs; a variable the run started with
// that is marked `tunable` takes its new value from it (#75), the others stay as they were.
// `speed`: where the run looks for a new speed while it runs (#219); it paces on at that speed
// from where it is, and says so with a run.speed. `cache`: a run that evaluates once reuses
// the outputs of blocks nothing changed for since an earlier run with the same cache
// (cache.hpp), and keeps its own; null: none.
std::string run(const std::string& id, const json& graph, const json& station, const Backend& backend,
                const Emit& emit, const KeysNow& keys, const std::stop_token& stop, const json& settings = nullptr,
                const json& variables = nullptr, const VariablesNow& tune = nullptr, const SpeedNow& speed = nullptr,
                RunCache* cache = nullptr);

}  // namespace paperboat::core
