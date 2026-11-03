#include "paperboat/core/run.hpp"

#include "paperboat/core/executor.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <thread>

namespace paperboat::core {

namespace {

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

}  // namespace

Calling::Calling(toolkit::Context& ctx, std::string_view node, std::string& failed)
    : ctx_(ctx), failed_(failed), exceptions_(std::uncaught_exceptions()) {
  ctx_.node = node;
}

Calling::~Calling() {
  if (std::uncaught_exceptions() > exceptions_ && failed_.empty()) failed_ = std::string(ctx_.node);
  ctx_.node = {};
}

Run::Run(std::string id, const Backend& backend, RunSettings settings, Emit emit, KeysNow keys, std::stop_token stop)
    : id_(std::move(id)),
      backend_(backend),
      settings_(settings),
      emit_(std::move(emit)),
      keys_now_(std::move(keys)),
      stop_(std::move(stop)) {
  ctx_.dt = settings_.dt;
  ctx_.keys_down = &keys_;
  log_ = [this](std::string_view node, std::string_view level, std::string_view text) {
    json msg = log_msg(std::string(level), std::string(text));
    if (!node.empty()) msg["node"] = node;
    emit_(msg);
  };
  ctx_.log_fn = &log_;
}

std::string Run::go(const json& graph_json, const json& station_json) {
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
      if (story.reset) {
        // Fresh blocks each time a path arrives: a story run twice (in a loop) starts over.
        story.step->on_enter = [this, order = &story.order] {
          for (const auto& id : *order) {
            auto& node = g_.nodes.at(id);
            const auto call = calling(id);
            if (node.block) node.block = node.entry->make(node.params, node.text);
            if (node.monitor) node.monitor = node.entry->make(node.params, node.text);
            outputs_.erase(id);
          }
        };
      }
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
    auto executor = backend_.executor ? backend_.executor(g_, settings_) : nullptr;
    if (!executor) executor = default_executor(g_, settings_);
    return finish(executor->execute(*this));
  } catch (const GraphError& err) {  // a path looped, a Step flow without a hat…
    fail(err.node.empty() ? failed_ : err.node, err.what());
    return finish("failed");
  } catch (const std::exception& err) {  // a block found its inputs unusable, or failed:
    fail(failed_, err.what());          // the run fails, never the backend
    return finish("failed");
  }
}

// An error, tagged with the block (or station object) at fault, which the UI marks.
void Run::fail(const std::string& node, const std::string& text) {
  json msg = log_msg("error", node.empty() ? text : node + ": " + text);
  if (!node.empty()) {
    msg["node"] = node;
    node_state(node, "failed");
  }
  emit_(msg);
}

std::string Run::finish(const std::string& status) {
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

bool Run::cancelled() {
  if (!stop_.stop_requested()) return false;
  emit_(log_msg("warn", "run cancelled"));
  return true;
}

void Run::log(std::string_view level, std::string_view text) { emit_(log_msg(std::string(level), std::string(text))); }

void Run::node_state(std::string_view node, std::string_view state) {
  emit_({{"type", "node.state"}, {"node", node}, {"state", state}});
}

void Run::at_time(double t) {
  ctx_.t = t;
  if (keys_now_) keys_ = keys_now_();
}

toolkit::Values Run::inputs_of(const Node& node) {
  toolkit::Values in;  // unconnected inputs stay absent and read as 0
  for (const auto& port : node.def.inputs) {
    auto wire = g_.wires.find({node.id, port.id});
    if (wire == g_.wires.end()) continue;
    const auto& from = outputs_[wire->second.first];
    if (auto v = from.find(wire->second.second); v != from.end()) in[port.id] = v->second;
  }
  return in;
}

json Run::pass() { return pass_over(g_.order); }

json Run::pass_over(const std::vector<std::string>& order) {
  json samples = json::array();
  for (const auto& id : order) {
    auto& node = g_.nodes.at(id);
    const auto call = calling(id);
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

void Run::store_step_outputs() {
  for (auto& [id, node] : g_.nodes)
    if (node.step) {
      const auto call = calling(id);
      if (auto values = node.step->outputs(); !values.empty()) outputs_[id] = std::move(values);
    }
}

std::optional<std::string> Run::advance() {
  for (auto& item : station_) {
    const auto call = calling(item.id);
    auto linked = g_.nodes.find(item.id);
    item.instance->step(ctx_, linked != g_.nodes.end() && linked->second.object ? inputs_of(linked->second) : toolkit::Values{});
  }
  return state_ ? state_->step(ctx_) : std::nullopt;
}

void Run::frame(const json& samples, std::optional<double> progress) {
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
  if (settings_.real_time)
    std::this_thread::sleep_for(std::chrono::duration<double>(settings_.dt * settings_.emit_every));
}

json Run::scene(bool with_blocks) const {
  return scene_message(station_, state_.get(), backend_.view, with_blocks ? blocks() : std::vector<const toolkit::Block*>{});
}

std::vector<const toolkit::Block*> Run::blocks() const {
  std::vector<const toolkit::Block*> out;
  for (const auto& [id, node] : g_.nodes)
    if (node.block) out.push_back(node.block.get());
  return out;
}

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
  return Run(id, backend, backend.run, emit, keys, stop).go(graph, station);
}

}  // namespace paperboat::core
