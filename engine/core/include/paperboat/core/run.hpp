#pragma once

// A run: a graph on a station and the domain's state. Knows nothing of sockets: it reports
// through `emit`, so a session sends the events and a test collects them.
//
// Run styles (Backend::run.mode), so a backend needn't be a simulation:
// - Clock: step everything on a clock (a Signal flow, or a Step flow if the graph has step
//   blocks), streaming signals and scene patches.
// - Once: evaluate a Signal flow a single time (a build, a query, a geometry), then show
//   the result. Wires may carry lists, tables and text.

#include <functional>
#include <stop_token>
#include <string>

#include "paperboat/core/graph.hpp"

namespace paperboat::core {

struct RunSettings {
  enum class Mode { Clock, Once };
  Mode mode = Mode::Clock;
  double dt = 0.01;        // clock step, s
  double duration = 10.0;  // run length (a Step flow: its limit), s
  int emit_every = 4;      // send signals and scene patches every N steps
  bool real_time = true;   // pace emits at wall-clock speed (tests turn it off)
};

// What a backend is made of. A simple backend fills one and calls serve() (backend.hpp).
struct Backend {
  std::string engine;                         // the name in `hello`
  const toolkit::Catalog* catalog = nullptr;  // with its starters; outlives the backend
  // A fresh domain state per run (and one for the opening scene); empty if none.
  std::function<std::unique_ptr<toolkit::DomainState>()> state = {};
  json view = nullptr;                        // the scene's camera hint ({"mode": "2d", …})
  RunSettings run = {};
};

using Emit = std::function<void(const json& message)>;
using KeysNow = std::function<toolkit::KeySet()>;  // keys held down in the UI right now

// The scene: station objects, the domain state's entities and what blocks show.
json scene_message(const Station& station, const toolkit::DomainState* state, const json& view,
                   const std::vector<const toolkit::Block*>& blocks = {});

// Emits run.started, then scene, signal, scene.patch, node.state, run.progress and log
// events, then run.finished. Returns the final status: "done", "failed" or "cancelled".
std::string run(const std::string& id, const json& graph, const json& station, const Backend& backend,
                const Emit& emit, const KeysNow& keys, const std::stop_token& stop);

}  // namespace paperboat::core
