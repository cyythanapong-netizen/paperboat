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
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

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
  Run(std::string id, const Backend& backend, RunSettings settings, Emit emit, KeysNow keys, std::stop_token stop);

  // Makes the station and graph, runs them with the backend's executor (or the default)
  // and reports the end. Returns "done", "failed" or "cancelled".
  std::string go(const json& graph, const json& station);

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
  // What step blocks report (a progress, a lap), for the next pass; or what one step block
  // reports now, for the blocks after it on the path.
  void store_step_outputs();
  void store_step_outputs(const std::string& node);
  // After a pass: station objects move under their linked blocks' inputs, then the domain
  // state. Returns a message if the domain ends the run.
  std::optional<std::string> advance();
  // Sends samples, the scene's changes and the run's progress; waits to keep real time.
  void frame(const json& samples, std::optional<double> progress);
  // The whole scene, with what blocks show (after a run-once).
  json scene(bool with_blocks) const;

  // The block at node `node` is being called (see Calling).
  [[nodiscard]] Calling calling(std::string_view node) { return {ctx_, node, failed_}; }
  toolkit::Values inputs_of(const Node& node);

  // True once the user cancelled; logs it. The executor then returns "cancelled".
  bool cancelled();
  void emit(const json& message) { emit_(message); }
  void log(std::string_view level, std::string_view text);
  void node_state(std::string_view node, std::string_view state);

 private:
  std::string finish(const std::string& status);
  void fail(const std::string& node, const std::string& text);
  std::vector<const toolkit::Block*> blocks() const;

  std::string id_;
  const Backend& backend_;
  RunSettings settings_;
  Emit emit_;
  KeysNow keys_now_;
  std::stop_token stop_;
  toolkit::KeySet keys_;
  std::string failed_;  // the block that threw, if one did
  toolkit::LogFn log_;
  toolkit::ProgressFn progress_;
  std::map<std::string, std::chrono::steady_clock::time_point, std::less<>> progress_sent_;  // node → last node.progress
  toolkit::Context ctx_;
  Station station_;
  std::unique_ptr<toolkit::DomainState> state_;
  Graph g_;
  std::map<std::string, toolkit::Values> outputs_;
  std::map<std::string, std::vector<json>> story_samples_;  // monitor → its latest story pass's samples, until the next frame
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
