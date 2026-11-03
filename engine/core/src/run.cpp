#include "paperboat/core/run.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <set>
#include <thread>

namespace paperboat::core {

namespace {

constexpr int kMaxHops = 100;  // instant blocks one path passes in a clock step: more is a loop

json log_msg(const std::string& level, const std::string& text) {
  return {{"type", "log"}, {"level", level}, {"text", text}};
}

std::string format_values(const toolkit::Values& values) {
  std::string out;
  char buf[64];
  for (const auto& [k, v] : values) {
    if (const double* number = std::get_if<double>(&v)) {
      std::snprintf(buf, sizeof buf, "%s%s = %.4g", out.empty() ? "" : ", ", k.c_str(), *number);
      out += buf;
    }
  }
  return out;
}

json entities_json(const std::vector<toolkit::Entity>& list) {
  json out = json::array();
  for (const auto& e : list) out.push_back(entity_json(e));
  return out;
}

// While alive, Context::node names the block (or station object) being called, so what it
// logs is tagged with its id. If the call throws, the id is kept in `failed`: the run
// reports which block failed.
class At {
 public:
  At(toolkit::Context& ctx, std::string_view node, std::string& failed)
      : ctx_(ctx), failed_(failed), exceptions_(std::uncaught_exceptions()) {
    ctx_.node = node;
  }
  ~At() {
    if (std::uncaught_exceptions() > exceptions_ && failed_.empty()) failed_ = std::string(ctx_.node);
    ctx_.node = {};
  }
  At(const At&) = delete;
  At& operator=(const At&) = delete;

 private:
  toolkit::Context& ctx_;
  std::string& failed_;
  int exceptions_;
};

// One run in progress: the graph, the station, the domain state and the outputs so far.
class Runner {
 public:
  Runner(const std::string& id, const Backend& backend, const Emit& emit, const KeysNow& keys, const std::stop_token& stop)
      : id_(id), backend_(backend), emit_(emit), keys_now_(keys), stop_(stop) {
    ctx_.dt = backend.run.dt;
    ctx_.keys_down = &keys_;
    log_ = [this](std::string_view node, std::string_view level, std::string_view text) {
      json msg = log_msg(std::string(level), std::string(text));
      if (!node.empty()) msg["node"] = node;
      emit_(msg);
    };
    ctx_.log_fn = &log_;
  }

  std::string go(const json& graph_json, const json& station_json) {
    emit_({{"type", "run.started"}, {"run", id_}});
    try {
      station_ = make_station(station_json, *backend_.catalog);
      if (backend_.state) state_ = backend_.state();
      ctx_.state = state_.get();
      g_ = prepare_graph(graph_json, *backend_.catalog, station_);
      for (auto& [gid, story] : g_.stories) {
        // A story's pass: its blocks, once. Its monitors' latest samples wait for the next frame.
        story.step->pass = [this, order = &story.order] {
          std::map<std::string, std::vector<json>> latest;  // this pass's samples, by monitor
          for (const auto& sample : pass_over(*order)) latest[sample["node"]].push_back(sample);
          for (auto& [node, list] : latest) story_samples_[node] = std::move(list);
        };
      }
    } catch (const GraphError& err) {
      fail(err.node, std::string("invalid graph: ") + err.what());
      return finish("failed");
    } catch (const std::exception& err) {  // malformed JSON
      emit_(log_msg("error", std::string("invalid graph: ") + err.what()));
      return finish("failed");
    }
    emit_(scene_message(station_, state_.get(), backend_.view));
    try {
      if (g_.has_steps) return steps();  // a Step flow (or stories) needs the clock
      if (backend_.run.mode == RunSettings::Mode::Once) return once();
      return clock();
    } catch (const GraphError& err) {  // a path looped, a Step flow without a hat…
      fail(err.node.empty() ? failed_ : err.node, err.what());
      return finish("failed");
    } catch (const std::exception& err) {  // a block found its inputs unusable, or failed:
      fail(failed_, err.what());          // the run fails, never the backend
      return finish("failed");
    }
  }

  // An error, tagged with the block (or station object) at fault, which the UI marks.
  void fail(const std::string& node, const std::string& text) {
    json msg = log_msg("error", node.empty() ? text : node + ": " + text);
    if (!node.empty()) {
      msg["node"] = node;
      emit_({{"type", "node.state"}, {"node", node}, {"state", "failed"}});
    }
    emit_(msg);
  }

 private:
  std::string finish(const std::string& status) {
    if (status == "done") {
      if (state_)
        if (auto summary = state_->summary()) emit_(log_msg("info", *summary));
      for (const auto& item : station_)
        if (auto values = format_values(item.instance->read()); !values.empty()) emit_(log_msg("info", item.id + ": " + values));
      emit_({{"type", "run.progress"}, {"run", id_}, {"fraction", 1.0}});
    }
    emit_({{"type", "run.finished"}, {"run", id_}, {"status", status}});
    return status;
  }

  bool cancelled() {
    if (!stop_.stop_requested()) return false;
    emit_(log_msg("warn", "run cancelled"));
    finish("cancelled");
    return true;
  }

  toolkit::Values inputs_of(const Node& node) {
    toolkit::Values in;  // unconnected inputs stay absent and read as 0
    for (const auto& port : node.entry->def.inputs) {
      auto wire = g_.wires.find({node.id, port.id});
      if (wire == g_.wires.end()) continue;
      const auto& from = outputs_[wire->second.first];
      if (auto v = from.find(wire->second.second); v != from.end()) in[port.id] = v->second;
    }
    return in;
  }

  // Every signal block outside stories once, in order. Returns the monitors' samples: a
  // number at the run clock, or a list by index (Once).
  json signal_pass() { return pass_over(g_.order); }

  json pass_over(const std::vector<std::string>& order) {
    json samples = json::array();
    for (const auto& id : order) {
      auto& node = g_.nodes.at(id);
      const At at(ctx_, id, failed_);
      if (node.object) {
        outputs_[id] = node.object->read();
      } else if (node.block) {
        outputs_[id] = node.block->step(ctx_, inputs_of(node));
      } else {  // a monitor: what it shows is its "value" output, or its input
        auto in = inputs_of(node);
        if (node.monitor)
          if (auto shown = node.monitor->step(ctx_, in); shown.contains("value")) in["in"] = shown.at("value");
        if (const auto* text = toolkit::get_if<std::string>(in, "in")) {
          samples.push_back({{"type", "signal"}, {"node", id}, {"t", std::round(ctx_.t * 1e4) / 1e4}, {"v", *text}});
        } else if (const auto* list = toolkit::get_if<toolkit::List>(in, "in")) {
          for (std::size_t i = 0; i < list->size(); ++i)
            samples.push_back({{"type", "signal"}, {"node", id}, {"t", i}, {"v", (*list)[i]}});
        } else {
          samples.push_back({{"type", "signal"}, {"node", id}, {"t", std::round(ctx_.t * 1e4) / 1e4},
                             {"v", toolkit::get_or(in, "in")}});
        }
      }
    }
    return samples;
  }

  // After a pass: station objects move under their linked blocks' inputs, then the domain
  // state. Returns a message if the domain ends the run.
  std::optional<std::string> advance() {
    for (auto& item : station_) {
      const At at(ctx_, item.id, failed_);
      auto linked = g_.nodes.find(item.id);
      item.instance->step(ctx_, linked != g_.nodes.end() && linked->second.object ? inputs_of(linked->second) : toolkit::Values{});
    }
    return state_ ? state_->step(ctx_) : std::nullopt;
  }

  void emit_frame(const json& samples, std::optional<double> progress) {
    for (const auto& s : samples) emit_(s);
    for (const auto& [node, list] : story_samples_)
      for (const auto& sample : list) emit_(sample);
    story_samples_.clear();
    json patch = scene_of(station_);
    if (state_)
      for (const auto& e : entities_json(state_->patch())) patch.push_back(e);
    for (const auto& e : scene_of_blocks(blocks())) patch.push_back(e);
    if (!patch.empty()) emit_({{"type", "scene.patch"}, {"entities", patch}});
    if (progress) emit_({{"type", "run.progress"}, {"run", id_}, {"fraction", *progress}});
    if (backend_.run.real_time)
      std::this_thread::sleep_for(std::chrono::duration<double>(backend_.run.dt * backend_.run.emit_every));
  }

  std::vector<const toolkit::Block*> blocks() const {
    std::vector<const toolkit::Block*> out;
    for (const auto& [id, node] : g_.nodes)
      if (node.block) out.push_back(node.block.get());
    return out;
  }

  void read_keys() {
    if (keys_now_) keys_ = keys_now_();
  }

  // A Signal flow on a clock.
  std::string clock() {
    const auto& s = backend_.run;
    char what[96];
    std::snprintf(what, sizeof what, "running %zu blocks on %zu station objects for %g s", g_.order.size(),
                  station_.size(), s.duration);
    emit_(log_msg("info", what));
    const int steps = static_cast<int>(std::lround(s.duration / s.dt));
    for (int k = 0; k <= steps; ++k) {
      if (cancelled()) return "cancelled";
      ctx_.t = k * s.dt;
      read_keys();
      const json samples = signal_pass();
      const auto ended = advance();
      if (k % s.emit_every == 0 || ended) emit_frame(samples, static_cast<double>(k) / steps);
      if (ended) {
        emit_(log_msg("warn", *ended));
        break;
      }
    }
    return finish("done");
  }

  // A Signal flow evaluated once: a build, a query, a geometry.
  std::string once() {
    read_keys();
    const json samples = signal_pass();
    for (const auto& s : samples) emit_(s);
    emit_(scene_message(station_, state_.get(), backend_.view, blocks()));
    emit_(log_msg("info", "evaluated " + std::to_string(g_.order.size()) + " blocks"));
    return finish("done");
  }

  // A Step flow: hats start paths; each path is at one block; a block another path is in
  // makes it wait; Stop ends everything.
  std::string steps() {
    const auto& s = backend_.run;
    if (g_.hats.empty()) throw GraphError("a Step flow needs a Start or When block");
    const bool listens = std::ranges::any_of(g_.hats, [&](const std::string& h) { return g_.nodes.at(h).step->may_start(); });
    char what[96];
    std::snprintf(what, sizeof what, "running; hats listen until Cancel, Stop or %g s", s.duration);
    emit_(log_msg("info", listens ? what : "running"));

    struct Path {
      std::string at;
      bool entered = false;
      std::string hat;
    };
    std::vector<Path> paths;
    std::set<std::string> busy;
    std::optional<std::string> stopped;
    auto node_state = [&](const std::string& node, const char* state) {
      emit_({{"type", "node.state"}, {"node", node}, {"state", state}});
    };

    for (int k = 0;; ++k) {
      if (cancelled()) return "cancelled";
      const double t = ctx_.t = k * s.dt;
      read_keys();
      const json samples = signal_pass();

      for (const auto& h : g_.hats) {  // a hat doesn't restart its path while that path runs
        auto& hat = g_.nodes.at(h);
        const At at(ctx_, h, failed_);
        const auto out = hat.step->tick(ctx_, inputs_of(hat));
        auto next = g_.nexts.find({h, out});
        if (!out.empty() && next != g_.nexts.end() && std::ranges::none_of(paths, [&](const Path& p) { return p.hat == h; }))
          paths.push_back({.at = next->second, .entered = false, .hat = h});
      }

      for (std::size_t i = 0; i < paths.size() && !stopped;) {
        auto& path = paths[i];
        bool ended = false;
        int hops = 0;
        for (; hops < kMaxHops; ++hops) {
          auto& node = g_.nodes.at(path.at);
          const std::string here = path.at;  // path.at changes when the path moves on
          const At at(ctx_, here, failed_);
          if (!path.entered) {
            if (busy.contains(path.at)) break;
            busy.insert(path.at);
            node.step->enter(ctx_, inputs_of(node));
            path.entered = true;
            node_state(path.at, "running");
            if (node.step->ends_run()) {
              stopped = path.at;
              break;
            }
          }
          const auto out = node.step->tick(ctx_, inputs_of(node));
          if (out.empty()) break;
          busy.erase(path.at);
          node_state(path.at, "done");
          auto next = g_.nexts.find({path.at, out});
          if (next == g_.nexts.end()) {
            ended = true;
            break;
          }
          path = {.at = next->second, .entered = false, .hat = path.hat};
        }
        if (hops == kMaxHops) throw GraphError(path.at, "a path went round a loop at " + path.at + " without a block that takes time");
        if (ended) paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
      }

      for (auto& [id, node] : g_.nodes)  // what steps report (a progress), for the next pass
        if (node.step) {
          const At at(ctx_, id, failed_);
          if (auto values = node.step->outputs(); !values.empty()) outputs_[id] = std::move(values);
        }
      const auto domain_ended = advance();
      const bool over = stopped || domain_ended ||
                        (paths.empty() && std::ranges::none_of(g_.hats, [&](const std::string& h) {
                           return g_.nodes.at(h).step->may_start();
                         }));
      if (k % s.emit_every == 0 || over) emit_frame(samples, std::nullopt);
      char at[64];
      std::snprintf(at, sizeof at, " at t = %.2f s", t);
      if (stopped) {
        emit_(log_msg("info", "stopped by " + *stopped + at));
        break;
      }
      if (domain_ended) {
        emit_(log_msg("warn", *domain_ended));
        break;
      }
      if (over) {
        emit_(log_msg("info", std::string("every path ended") + at));
        break;
      }
      if (t >= s.duration) {
        char limit[64];
        std::snprintf(limit, sizeof limit, "stopped after %g s", s.duration);
        emit_(log_msg("info", limit));
        break;
      }
    }
    return finish("done");
  }

  std::string id_;
  const Backend& backend_;
  const Emit& emit_;
  const KeysNow& keys_now_;
  const std::stop_token& stop_;
  toolkit::KeySet keys_;
  std::string failed_;  // the block that threw, if one did
  toolkit::LogFn log_;
  toolkit::Context ctx_;
  Station station_;
  std::unique_ptr<toolkit::DomainState> state_;
  Graph g_;
  std::map<std::string, toolkit::Values> outputs_;
  std::map<std::string, std::vector<json>> story_samples_;  // monitor → its latest story pass's samples, until the next frame
};

}  // namespace

json scene_message(const Station& station, const toolkit::DomainState* state, const json& view,
                   const std::vector<const toolkit::Block*>& blocks) {
  json entities = scene_of(station);
  if (state)
    for (const auto& e : entities_json(state->entities())) entities.push_back(e);
  for (const auto& e : scene_of_blocks(blocks)) entities.push_back(e);
  json msg = {{"type", "scene"}, {"entities", entities}};
  if (!view.is_null()) msg["view"] = view;
  return msg;
}

std::string run(const std::string& id, const json& graph, const json& station, const Backend& backend,
                const Emit& emit, const KeysNow& keys, const std::stop_token& stop) {
  return Runner(id, backend, emit, keys, stop).go(graph, station);
}

}  // namespace paperboat::core
