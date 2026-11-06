#include "paperboat/core/run.hpp"

#include "paperboat/core/executor.hpp"

#include <paperboat/toolkit/variables.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
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

// Whether `id` is a block of `graph` or an object of `station` (else a variable's name).
bool names_node(const json& graph, const json& station, const std::string& id) {
  auto has = [&](const json& doc, const char* list) {
    if (!doc.is_object() || !doc.contains(list)) return false;
    return std::ranges::any_of(doc.at(list), [&](const json& n) { return n.value("id", std::string{}) == id; });
  };
  return !id.empty() && (has(graph, "nodes") || has(station, "objects"));
}

json entities_json(const std::vector<toolkit::Entity>& list) {
  json out = json::array();
  for (const auto& e : list) out.push_back(entity_json(e));
  return out;
}

void validate_vector_outputs(const Node& node, const toolkit::Values& values) {
  for (const auto& port : node.def.outputs) {
    const auto value = values.find(port.id);
    if (value == values.end()) continue;
    if (const auto mismatch = toolkit::vector_value_mismatch(port, value->second))
      throw GraphError(node.id, "output " + node.id + "." + port.id + " " + *mismatch);
  }
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
  progress_ = [this](std::string_view node, double fraction) {
    if (node.empty() || !std::isfinite(fraction)) return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    const auto now = std::chrono::steady_clock::now();
    auto last = progress_sent_.find(node);
    if (last != progress_sent_.end() && fraction < 1 && now - last->second < std::chrono::milliseconds(50)) return;
    progress_sent_.insert_or_assign(std::string(node), now);
    emit_({{"type", "node.progress"}, {"node", node}, {"fraction", fraction}});
  };
  ctx_.progress_fn = &progress_;
}

std::string Run::go(const json& graph_given, const json& station_given, const json& variables) {
  run_start_time_ = std::chrono::steady_clock::now();
  run_started_time_set_ = true;
  last_lag_warn_ = run_start_time_;
  emit_({{"type", "run.started"}, {"run", id_}});
  json graph_json, station_json;
  try {  // variables first (#36): blocks and objects get the values they stand for
    const toolkit::Variables vars(variables);
    graph_json = toolkit::resolve_graph(graph_given, *backend_.catalog, vars);
    station_json = toolkit::resolve_station(station_given, *backend_.catalog, vars);
  } catch (const toolkit::VariableError& err) {
    fail(names_node(graph_given, station_given, err.where) ? err.where : "", std::string("invalid variables: ") + err.what());
    return finish("failed");
  }
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
      if (!story.release.empty()) {
        // Released outputs read as unwired once the path leaves: a story that drives
        // something stops driving it, while its results hold for what comes after.
        story.step->on_leave = [this, release = &story.release] {
          for (const auto& [node, port] : *release)
            if (auto it = outputs_.find(node); it != outputs_.end()) it->second.erase(port);
        };
      }
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
    if (settings_.duration > 0) emit_({{"type", "run.progress"}, {"run", id_}, {"fraction", 1.0}});
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
    if (auto v = from.find(wire->second.second); v != from.end()) {
      if (const auto mismatch = toolkit::vector_value_mismatch(port, v->second))
        throw GraphError(node.id, "input " + node.id + "." + port.id + " " + *mismatch);
      in[port.id] = v->second;
    }
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
      validate_vector_outputs(node, outputs_[id]);
    } else if (node.block && !node.def.feedthrough) {
      outputs_[id] = node.block->outputs();  // its inputs come after the pass
      validate_vector_outputs(node, outputs_[id]);
    } else if (node.block) {
      outputs_[id] = node.block->step(ctx_, inputs_of(node));
      validate_vector_outputs(node, outputs_[id]);
    } else {  // a monitor: what it shows is its "value" output, or its input
      auto in = inputs_of(node);
      if (node.monitor)
        if (auto shown = node.monitor->step(ctx_, in); shown.contains("value")) in["in"] = shown.at("value");
      if (const auto* text = toolkit::get_if<std::string>(in, "in")) {
        samples.push_back({{"type", "signal"}, {"node", id}, {"t", std::round(ctx_.t * 1e4) / 1e4}, {"v", *text}});
      } else if (const auto* vector = toolkit::get_vector(in, "in")) {
        if (node.def.instrument == "display") {
          // A Display is a readout: preserve the vector as one value. A Graph keeps its
          // existing list-as-samples behavior (index is x), useful for point series.
          json sample = {{"type", "signal"}, {"node", id}, {"t", std::round(ctx_.t * 1e4) / 1e4}, {"v", *vector}};
          if (const auto wire = g_.wires.find({id, "in"}); wire != g_.wires.end()) {
            const auto& source = g_.nodes.at(wire->second.first);
            const auto port = std::ranges::find(source.def.outputs, wire->second.second, &toolkit::PortDef::id);
            if (port != source.def.outputs.end() && !port->components.empty()) {
              json components = json::array();
              for (const auto& component : port->components) {
                json item = {{"id", component.id}};
                if (!component.unit.empty()) item["unit"] = component.unit;
                components.push_back(std::move(item));
              }
              sample["components"] = std::move(components);
            }
          }
          samples.push_back(std::move(sample));
        } else {
          for (std::size_t i = 0; i < vector->size(); ++i)
            samples.push_back({{"type", "signal"}, {"node", id}, {"t", i}, {"v", (*vector)[i]}});
        }
      } else {
        samples.push_back({{"type", "signal"}, {"node", id}, {"t", std::round(ctx_.t * 1e4) / 1e4},
                           {"v", toolkit::get_or(in, "in")}});
      }
    }
  }
  // Blocks without feedthrough take this pass's inputs now that every block has run.
  for (const auto& id : order) {
    auto& node = g_.nodes.at(id);
    if (!node.block || node.def.feedthrough) continue;
    const auto call = calling(id);
    node.block->step(ctx_, inputs_of(node));
  }
  return samples;
}

void Run::store_step_outputs() {
  for (auto& [id, node] : g_.nodes)
    if (node.step) store_step_outputs(id);
}

void Run::store_step_outputs(const std::string& id) {
  const auto call = calling(id);
  if (auto values = g_.nodes.at(id).step->outputs(); !values.empty()) {
    validate_vector_outputs(g_.nodes.at(id), values);
    outputs_[id] = std::move(values);
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
  if (settings_.real_time && settings_.speed > 0) {
    if (!run_started_time_set_) {
      run_start_time_ = std::chrono::steady_clock::now();
      run_started_time_set_ = true;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto target_time = run_start_time_ + std::chrono::duration<double>(ctx_.t / settings_.speed);
    if (target_time > now) {
      std::this_thread::sleep_for(target_time - now);
    } else {
      const auto lag = std::chrono::duration<double>(now - target_time).count();
      if (lag > 0.25 && (now - last_lag_warn_ > std::chrono::seconds(2))) {
        last_lag_warn_ = now;
        char buf[96];
        std::snprintf(buf, sizeof buf, "computation delay: behind real-time by %.2f s", lag);
        log("warn", buf);
      }
    }
  }
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

RunSettings with_overrides(RunSettings base, const json& settings) {
  if (settings.is_null()) return base;
  if (!settings.is_object()) throw std::invalid_argument("run settings must be an object");
  auto number = [&](const char* key, double& into, bool zero_ok) {
    if (!settings.contains(key)) return;
    const auto& v = settings.at(key);
    if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() < 0 || (!zero_ok && v.get<double>() == 0))
      throw std::invalid_argument(std::string("run setting ") + key + (zero_ok ? " must be 0 or more" : " must be more than 0"));
    into = v.get<double>();
  };
  number("duration", base.duration, true);
  number("dt", base.dt, false);
  number("speed", base.speed, true);
  if (base.duration > 0 && base.duration / base.dt > 1e7)
    throw std::invalid_argument("run settings: more than 10 million clock steps (duration / dt)");
  return base;
}

json settings_json(const RunSettings& s) {
  return {{"mode", s.mode == RunSettings::Mode::Once ? "once" : "clock"}, {"duration", s.duration}, {"dt", s.dt}, {"speed", s.speed}};
}

std::string run(const std::string& id, const json& graph, const json& station, const Backend& backend,
                const Emit& emit, const KeysNow& keys, const std::stop_token& stop, const json& settings,
                const json& variables) {
  RunSettings s;
  try {
    s = with_overrides(backend.run, settings);
  } catch (const std::invalid_argument& err) {
    emit({{"type", "run.started"}, {"run", id}});
    emit(log_msg("error", err.what()));
    emit({{"type", "run.finished"}, {"run", id}, {"status", "failed"}});
    return "failed";
  }
  return Run(id, backend, s, emit, keys, stop).go(graph, station, variables);
}

}  // namespace paperboat::core
