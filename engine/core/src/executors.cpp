// The core's run styles (executor.hpp): clock, once and steps.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "paperboat/core/executor.hpp"

namespace paperboat::core {
namespace {

constexpr int kMaxHops = 100;  // instant blocks one path passes in a clock step: more is a loop

// A Signal flow on a clock: every block once per step, for the run's duration.
class Clock final : public Executor {
 public:
  std::string execute(Run& run) override {
    const auto& s = run.settings();
    char what[96];
    std::snprintf(what, sizeof what, "running %zu blocks on %zu station objects for %g s", run.graph().order.size(),
                  run.station().size(), s.duration);
    run.log("info", what);
    const int steps = static_cast<int>(std::lround(s.duration / s.dt));
    for (int k = 0; k <= steps; ++k) {
      if (run.cancelled()) return "cancelled";
      run.at_time(k * s.dt);
      const json samples = run.pass();
      const auto ended = run.advance();
      if (k % s.emit_every == 0 || k == steps || ended) run.frame(samples, steps ? static_cast<double>(k) / steps : 1.0);
      if (ended) {
        run.log("warn", *ended);
        break;
      }
    }
    return "done";
  }
};

// A Signal flow evaluated once: a build, a query, a geometry.
class Once final : public Executor {
 public:
  std::string execute(Run& run) override {
    run.at_time(0);
    const json samples = run.pass();
    for (const auto& s : samples) run.emit(s);
    run.emit(run.scene(true));
    run.log("info", "evaluated " + std::to_string(run.graph().order.size()) + " blocks");
    return "done";
  }
};

// A Step flow: hats start paths; each path is at one block; a block another path is in
// makes it wait; Stop ends everything.
class Steps final : public Executor {
 public:
  std::string execute(Run& run) override {
    auto& g = run.graph();
    auto& ctx = run.context();
    const auto& s = run.settings();
    if (g.hats.empty()) throw GraphError("a Step flow needs a Start or When block");
    const bool listens = std::ranges::any_of(g.hats, [&](const std::string& h) { return g.nodes.at(h).step->may_start(); });
    char what[96];
    std::snprintf(what, sizeof what, "running; hats listen until Cancel, Stop or %g s", s.duration);
    run.log("info", listens ? what : "running");

    struct Path {
      std::string at;
      bool entered = false;
      std::string hat;
    };
    std::vector<Path> paths;
    std::set<std::string> busy;
    std::optional<std::string> stopped;

    for (int k = 0;; ++k) {
      if (run.cancelled()) return "cancelled";
      const double t = k * s.dt;
      run.at_time(t);
      const json samples = run.pass();

      for (const auto& h : g.hats) {  // a hat doesn't restart its path while that path runs
        auto& hat = g.nodes.at(h);
        const auto call = run.calling(h);
        const auto out = hat.step->tick(ctx, run.inputs_of(hat));
        auto next = g.nexts.find({h, out});
        if (!out.empty() && next != g.nexts.end() && std::ranges::none_of(paths, [&](const Path& p) { return p.hat == h; }))
          paths.push_back({.at = next->second, .entered = false, .hat = h});
      }

      for (std::size_t i = 0; i < paths.size() && !stopped;) {
        auto& path = paths[i];
        bool ended = false;
        int hops = 0;
        for (; hops < kMaxHops; ++hops) {
          auto& node = g.nodes.at(path.at);
          const std::string here = path.at;  // path.at changes when the path moves on
          const auto call = run.calling(here);
          if (!path.entered) {
            if (busy.contains(path.at)) break;
            busy.insert(path.at);
            node.step->enter(ctx, run.inputs_of(node));
            path.entered = true;
            run.node_state(path.at, "running");
            if (node.step->ends_run()) {
              stopped = path.at;
              break;
            }
          }
          const auto out = node.step->tick(ctx, run.inputs_of(node));
          if (out.empty()) break;
          busy.erase(path.at);
          run.node_state(path.at, "done");
          auto next = g.nexts.find({path.at, out});
          if (next == g.nexts.end()) {
            ended = true;
            break;
          }
          path = {.at = next->second, .entered = false, .hat = path.hat};
        }
        if (hops == kMaxHops) throw GraphError(path.at, "a path went round a loop at " + path.at + " without a block that takes time");
        if (ended) paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
      }

      run.store_step_outputs();
      const auto domain_ended = run.advance();
      const bool over = stopped || domain_ended ||
                        (paths.empty() && std::ranges::none_of(g.hats, [&](const std::string& h) {
                           return g.nodes.at(h).step->may_start();
                         }));
      if (k % s.emit_every == 0 || over) run.frame(samples, std::nullopt);
      char at[64];
      std::snprintf(at, sizeof at, " at t = %.2f s", t);
      if (stopped) {
        run.log("info", "stopped by " + *stopped + at);
        break;
      }
      if (domain_ended) {
        run.log("warn", *domain_ended);
        break;
      }
      if (over) {
        run.log("info", std::string("every path ended") + at);
        break;
      }
      if (t >= s.duration) {
        char limit[64];
        std::snprintf(limit, sizeof limit, "stopped after %g s", s.duration);
        run.log("info", limit);
        break;
      }
    }
    return "done";
  }
};

}  // namespace

std::unique_ptr<Executor> clock_executor() { return std::make_unique<Clock>(); }
std::unique_ptr<Executor> once_executor() { return std::make_unique<Once>(); }
std::unique_ptr<Executor> step_executor() { return std::make_unique<Steps>(); }

std::unique_ptr<Executor> default_executor(const Graph& graph, const RunSettings& settings) {
  if (graph.has_steps) return step_executor();  // a Step flow (or stories) needs the clock
  return settings.mode == RunSettings::Mode::Once ? once_executor() : clock_executor();
}

}  // namespace paperboat::core
