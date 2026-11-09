#pragma once

// How a graph runs: an Executor drives a Run (the graph made, the station, the domain
// state and the outputs so far) from start to end. The core has three:
// - clock: a Signal flow, every block once per clock step, for the run's duration;
// - once: a Signal flow evaluated a single time (a build, a query, an image);
// - steps: a Step flow (or a graph with stories): hats start paths, a path is at one block.
// A backend with another run style (event-driven, a long job, an external solver) sets
// Backend::executor and drives the same Run; it never touches the socket.

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <paperboat/toolkit/variables.hpp>

#include "paperboat/core/cache.hpp"
#include "paperboat/core/run.hpp"

namespace paperboat::core {

// While alive, Context::node names the block (or station object) being called, so what it
// logs is tagged with its id. If the call throws, the id is kept: the run reports which
// block failed.
class Calling {
 public:
  Calling(toolkit::Context& ctx, std::string_view node, std::string& failed);
  ~Calling();
  Calling(const Calling&) = delete;
  Calling& operator=(const Calling&) = delete;

 private:
  toolkit::Context& ctx_;
  std::string& failed_;
  int exceptions_;
};

class Run {
 public:
  // `cache`: where a run that evaluates once finds the outputs of earlier runs and keeps its
  // own (see evaluate); null: every block is called.
  Run(std::string id, const Backend& backend, RunSettings settings, Emit emit, KeysNow keys, std::stop_token stop,
      VariablesNow tune = nullptr, SpeedNow speed = nullptr, RunCache* cache = nullptr);

  // Makes the station and graph (references to `variables` resolved first, #36), runs them
  // with the backend's executor (or the default) and reports the end. Returns "done",
  // "failed" or "cancelled".
  std::string go(const json& graph, const json& station, const json& variables = nullptr);

  // ---- For executors ----
  Graph& graph() { return g_; }
  const Station& station() const { return station_; }
  const RunSettings& settings() const { return settings_; }
  toolkit::Context& context() { return ctx_; }

  // Sets the run clock and reads the keys held in the UI now.
  void at_time(double t);
  // Every signal block outside stories, once, in order. Returns the monitors' samples
  // (signal messages, sent by frame()).
  json pass();
  json pass_over(const std::vector<std::string>& order);
  // One block of a run that evaluates once: its outputs from the run's cache when nothing it
  // depends on changed since a run computed them (node.state "done" alone, the logs it wrote
  // shown again), else called like pass_over({id}) (node.state "running", then "done") and, if
  // it may be reused, kept in the cache for the next run. Returns its samples.
  json evaluate(const std::string& id);
  // How many blocks evaluate() took from the cache so far.
  int reused() const { return reused_; }
  // What step blocks report (a progress, a lap), for the next pass; or what one step block
  // reports now, for the blocks after it on the path.
  void store_step_outputs();
  void store_step_outputs(const std::string& node);
  // After a pass: station objects move under their linked blocks' inputs, then the domain
  // state. Returns a message if the domain ends the run.
  std::optional<std::string> advance();
  // Sends samples, the scene's changes and the run's progress; waits to keep real time, at the
  // speed the UI asked for last (#219).
  void frame(const json& samples, std::optional<double> progress);
  // The whole scene, with what blocks show (after a run-once).
  json scene(bool with_blocks) const;

  // The block at node `node` is being called (see Calling).
  [[nodiscard]] Calling calling(std::string_view node) { return {ctx_, node, failed_}; }
  toolkit::Values inputs_of(const Node& node);

  // True once the user cancelled; logs it. The executor then returns "cancelled".
  bool cancelled();
  void emit(const json& message);  // a `scene` also resets what the UI holds of meshes (below)
  void log(std::string_view level, std::string_view text);
  void node_state(std::string_view node, std::string_view state);

 private:
  std::string finish(const std::string& status);
  void fail(const std::string& node, const std::string& text);
  std::vector<const toolkit::Block*> blocks() const;
  // Takes a changed variables list (#75): each tunable variable's new value reaches the Variable
  // blocks and the number params bound to it, between two steps.
  void tune(const json& list);
  // Takes `next` (a variables list) as the run's: formulas follow, bound params change on
  // their blocks. A list the variables refuse is ignored with a warning.
  void apply_variables(const json& next);
  // A block's Context::set_variable (#166): checked now, kept in `pending_` until the end of the pass.
  bool set_variable(std::string_view name, const toolkit::Value& value);
  // The end of a pass: applies what blocks set, and tells the UI (variables.patch).
  void commit_variables();
  bool change_param(Node& node, const toolkit::ParamDef& def, double value);
  // Mesh arrays (vertices, indices, values) the UI holds: a scene's are noted; a patch's that
  // the UI already holds are left out of it, so a mesh is sent again only when it changes.
  void note_arrays(const json& entities);
  void drop_sent_arrays(json& entities);
  // A new speed from the UI, if one came (#219): pacing goes on from where it is at that speed,
  // and the UI hears it took it (run.speed).
  void take_speed();
  // Waits until the wall clock reaches the run clock at the run's speed, taking a new speed
  // while it waits; warns when computation has fallen behind.
  void pace();

  // What a block whose outputs may be reused is, for the cache: type, ports, params, text,
  // lanes, the run's dt and solver, its files. None for a block that is always called.
  std::optional<std::string> cache_signature(const Node& node) const;

  // What the block evaluate() is calling depends on and does besides its outputs.
  struct Recording {
    std::string node;
    std::map<std::string, std::optional<toolkit::Value>, std::less<>> variables;  // read
    std::vector<std::pair<std::string, std::string>> logs;                        // written
    bool set_variable = false;  // a side effect: not reused
  };

  std::string id_;
  const Backend& backend_;
  RunSettings settings_;
  Emit emit_;
  KeysNow keys_now_;
  VariablesNow tune_now_;
  SpeedNow speed_now_;
  std::stop_token stop_;
  toolkit::KeySet keys_;
  std::string failed_;  // the block that threw, if one did
  toolkit::LogFn log_;
  toolkit::ProgressFn progress_;
  toolkit::SetVariableFn set_variable_;
  toolkit::VariableReadFn variable_read_;
  RunCache* cache_ = nullptr;
  std::optional<Recording> recording_;
  int reused_ = 0;
  bool evaluated_ = false;  // evaluate() ran: a run that ends done drops other nodes' entries
  json pending_;  // the variables list with this pass's set_variable writes; null when none
  std::map<std::string, std::chrono::steady_clock::time_point, std::less<>> progress_sent_;  // node → last node.progress
  // Pacing's anchor (#219): run time pace_t_ was due at wall time pace_wall_, at settings_.speed
  // since. A speed change moves it to now, so the run neither jumps ahead nor stalls.
  std::chrono::steady_clock::time_point pace_wall_{};
  double pace_t_ = 0;
  bool paced_ = false;
  std::chrono::steady_clock::time_point last_lag_warn_{};
  toolkit::Context ctx_;
  json graph_given_;               // as the UI sent it, references and all (to resolve again when a variable is tuned)
  json station_given_;
  json variables_given_;           // the variables list now: the run's own, with each tunable one's latest value
  toolkit::Variables vars_;        // ... resolved
  toolkit::Values live_variables_; // ... for Context::variable
  std::set<std::string> told_;     // "block.param" the user was told takes a change only with the next run
  Station station_;
  std::unique_ptr<toolkit::DomainState> state_;
  Graph g_;
  std::map<std::string, toolkit::Values> outputs_;
  std::map<std::string, std::vector<json>> story_samples_;  // monitor → its latest story pass's samples, until the next frame
  std::map<std::string, json> sent_arrays_;  // "entity\nfield" → the array the UI got last
};

class Executor {
 public:
  virtual ~Executor() = default;
  // Runs `run` to its end. Returns "done", or "cancelled" once run.cancelled() said so.
  // Throws to fail the run (a GraphError names the block at fault).
  virtual std::string execute(Run& run) = 0;
};

std::unique_ptr<Executor> clock_executor();
std::unique_ptr<Executor> once_executor();
std::unique_ptr<Executor> step_executor();

// The core's choice: steps if the graph has step blocks or stories, else once or clock by
// the settings' mode.
std::unique_ptr<Executor> default_executor(const Graph& graph, const RunSettings& settings);

}  // namespace paperboat::core
