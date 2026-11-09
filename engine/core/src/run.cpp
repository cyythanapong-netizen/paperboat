#include "paperboat/core/run.hpp"

#include "paperboat/core/executor.hpp"

#include <paperboat/toolkit/solve.hpp>
#include <paperboat/toolkit/variables.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <variant>

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

// What the Variable block reads: a number, a boolean as 1 or 0, the rest as text.
toolkit::Values variable_values(const toolkit::Variables& vars) {
  toolkit::Values out;
  const json all = vars.values();
  for (const auto& [name, v] : all.items()) {
    if (v.is_number()) out[name] = v.get<double>();
    else if (v.is_boolean()) out[name] = v.get<bool>() ? 1.0 : 0.0;
    else if (v.is_string()) out[name] = v.get<std::string>();
  }
  return out;
}

json entities_json(const std::vector<toolkit::Entity>& list) {
  json out = json::array();
  for (const auto& e : list) out.push_back(entity_json(e));
  return out;
}

// A block a run-once took from the cache: it gives the outputs and draws what it did then.
class Reused final : public toolkit::Block {
 public:
  explicit Reused(std::shared_ptr<const RunCache::Entry> entry) : entry_(std::move(entry)) {}
  toolkit::Values step(const toolkit::Context&, const toolkit::Values&) override { return entry_->outputs; }
  std::vector<toolkit::Entity> entities() const override { return entry_->entities; }
  bool stateless() const override { return true; }

 private:
  std::shared_ptr<const RunCache::Entry> entry_;
};

// A value in a cache signature: exact (a number keeps every digit; inf and nan are named).
json signature_value(const toolkit::Value& value) {
  const auto number = [](double x) -> json {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
    return x;
  };
  if (const double* x = std::get_if<double>(&value)) return number(*x);
  if (const auto* v = std::get_if<toolkit::Vector>(&value)) {
    json out = json::array();
    for (double x : *v) out.push_back(number(x));
    return out;
  }
  if (const auto* table = std::get_if<toolkit::Table>(&value)) {
    json out = json::array();
    for (const auto& row : *table) {
      json r = json::array();
      for (double x : row) r.push_back(number(x));
      out.push_back(std::move(r));
    }
    return out;
  }
  if (const auto* text = std::get_if<std::string>(&value)) return json{{"text", *text}};
  const auto& handle = std::get<toolkit::Handle>(value);
  return json{{"handle", handle.kind}, {"at", reinterpret_cast<std::uintptr_t>(handle.data.get())}};
}

json ports_json(const std::vector<toolkit::PortDef>& ports) {
  json out = json::array();
  for (const auto& p : ports) {
    json components = json::array();
    for (const auto& c : p.components) components.push_back({c.id, c.unit});
    out.push_back({p.id, p.type, p.unit, p.dimension ? json(*p.dimension) : json(), components, p.broadcast});
  }
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

// A frame (Context::frame) as its message's fields, or why it can't be sent.
bool is_finite_point(const std::array<double, 2>& p) { return std::isfinite(p[0]) && std::isfinite(p[1]); }

std::variant<json, std::string> frame_fields(const toolkit::Frame& frame) {
  static const std::set<std::string, std::less<>> kinds = {"line", "poly", "circle", "text"};
  static const std::set<std::string, std::less<>> strokes = {"", "ink", "muted", "signal", "danger"};
  static const std::set<std::string, std::less<>> fills = {"", "none", "ink", "raised", "sunken", "signal"};
  json out = json::object();
  if (frame.view) {
    const auto& v = *frame.view;
    if (!std::ranges::all_of(v, [](double x) { return std::isfinite(x); }) || v[2] <= v[0] || v[3] <= v[1])
      return std::string("its view isn't [xmin, ymin, xmax, ymax]");
    out["view"] = v;
  }
  if (frame.shapes.size() > static_cast<std::size_t>(toolkit::kFrameMaxShapes))
    return "more than " + std::to_string(toolkit::kFrameMaxShapes) + " shapes";
  if (!frame.shapes.empty()) {
    json shapes = json::array();
    for (const auto& shape : frame.shapes) {
      if (!kinds.contains(shape.t)) return "no shape \"" + shape.t + "\" (line, poly, circle, text)";
      if (!strokes.contains(shape.stroke)) return "no stroke \"" + shape.stroke + "\" (ink, muted, signal, danger)";
      if (!fills.contains(shape.fill)) return "no fill \"" + shape.fill + "\" (none, ink, raised, sunken, signal)";
      const std::size_t least = shape.t == "line" || shape.t == "poly" ? 2 : 1;
      if (shape.p.size() < least) return "a " + shape.t + " needs " + std::to_string(least) + (least == 1 ? " point" : " points");
      if (!std::ranges::all_of(shape.p, is_finite_point)) return "a " + shape.t + "'s point isn't finite";
      json item = {{"t", shape.t}, {"p", shape.p}};
      if (shape.t == "circle") {
        if (!std::isfinite(shape.r) || shape.r < 0) return std::string("a circle's radius isn't 0 or more");
        item["r"] = shape.r;
      }
      if (shape.t == "text") item["s"] = shape.s;
      if (!shape.stroke.empty()) item["stroke"] = shape.stroke;
      if (!shape.fill.empty()) item["fill"] = shape.fill;
      shapes.push_back(std::move(item));
    }
    out["shapes"] = std::move(shapes);
  }
  if (frame.field) {
    const auto& f = *frame.field;
    if (f.w <= 0 || f.h <= 0 || static_cast<long long>(f.w) * f.h > toolkit::kFrameMaxCells)
      return "its field isn't 1 to " + std::to_string(toolkit::kFrameMaxCells) + " cells";
    if (f.v.size() != static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h))
      return "its field has " + std::to_string(f.v.size()) + " values for " + std::to_string(f.w) + " × " + std::to_string(f.h) + " cells";
    json values = json::array();
    for (double x : f.v) values.push_back(std::isfinite(x) ? json(x) : json());  // no value: null
    json field = {{"w", f.w}, {"h", f.h}, {"v", std::move(values)}};
    if (f.min && std::isfinite(*f.min)) field["min"] = *f.min;
    if (f.max && std::isfinite(*f.max)) field["max"] = *f.max;
    out["field"] = std::move(field);
  }
  if (!out.contains("shapes") && !out.contains("field")) return std::string("it has neither shapes nor a field");
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

Run::Run(std::string id, const Backend& backend, RunSettings settings, Emit emit, KeysNow keys, std::stop_token stop,
         VariablesNow tune, RunCache* cache)
    : id_(std::move(id)),
      backend_(backend),
      settings_(settings),
      emit_(std::move(emit)),
      keys_now_(std::move(keys)),
      tune_now_(std::move(tune)),
      stop_(std::move(stop)),
      cache_(cache) {
  ctx_.dt = settings_.dt;
  ctx_.keys_down = &keys_;
  ctx_.variables = &live_variables_;
  log_ = [this](std::string_view node, std::string_view level, std::string_view text) {
    if (recording_ && node == recording_->node) recording_->logs.emplace_back(level, text);
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
  set_variable_ = [this](std::string_view name, const toolkit::Value& value) {
    if (recording_) recording_->set_variable = true;
    return set_variable(name, value);
  };
  ctx_.set_variable_fn = &set_variable_;
  variable_read_ = [this](std::string_view name, const toolkit::Value* value) {
    if (recording_ && ctx_.node == recording_->node)
      recording_->variables.try_emplace(std::string(name), value ? std::optional(*value) : std::nullopt);
  };
  ctx_.variable_read_fn = &variable_read_;
  metric_ = [this](std::string_view node, std::string_view series, std::optional<double> x, double y, std::string_view unit) {
    metric(node, series, x, y, unit);
  };
  ctx_.metric_fn = &metric_;
  monitor_state_ = [this](std::string_view node, toolkit::MonitorState state, double value, std::string_view port) {
    monitor_state(node, state, value, port);
  };
  ctx_.monitor_state_fn = &monitor_state_;
  frame_ = [this](std::string_view node, const toolkit::Frame& frame) { frame_of(node, frame); };
  ctx_.frame_fn = &frame_;
}

std::string Run::go(const json& graph_given, const json& station_given, const json& variables) {
  run_start_time_ = std::chrono::steady_clock::now();
  run_started_time_set_ = true;
  last_lag_warn_ = run_start_time_ - std::chrono::seconds(2);  // the first lag warns at once
  emit_({{"type", "run.started"}, {"run", id_}});
  json graph_json, station_json;
  try {  // variables first (#36): blocks and objects get the values they stand for
    vars_ = toolkit::Variables(variables);
    graph_json = toolkit::resolve_graph(graph_given, *backend_.catalog, vars_);
    station_json = toolkit::resolve_station(station_given, *backend_.catalog, vars_);
    graph_given_ = graph_given;
    station_given_ = station_given;
    variables_given_ = variables;
    live_variables_ = variable_values(vars_);
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
            if (node.block) node.block = make_block(node);
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
  flush_frames(true);  // a block's last picture, sent before the run ends
  if (status == "done" && cache_ && evaluated_) {  // what the graph no longer has needn't be kept
    std::set<std::string, std::less<>> ids;
    for (const auto& [id, node] : g_.nodes) ids.insert(id);
    cache_->keep_only(ids);
  }
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

void Run::warn_once(std::string_view node, const std::string& key, const std::string& text) {
  if (told_.insert(std::string(node) + "\n" + key).second) log_(node, "warn", text);
}

void Run::send_event(json message) {
  if (recording_ && message.value("node", std::string{}) == recording_->node) recording_->events.push_back(message);
  emit_(message);
}

// A sample's x: the run clock to 0.1 ms, as signals have it.
static double sample_time(double t) { return std::round(t * 1e4) / 1e4; }

void Run::metric(std::string_view node, std::string_view series, std::optional<double> x, double y, std::string_view unit) {
  if (node.empty()) return;
  if (series.empty()) return warn_once(node, "metric:name", "metric: a series needs a name");
  auto& next = metric_next_[std::string(node) + "\n" + std::string(series)];
  const double at = x.value_or(next);
  if (!x) ++next;  // the index moves on even past a point dropped, so the next keeps its place
  if (!std::isfinite(at) || !std::isfinite(y)) return;
  json msg = {{"type", "metric"}, {"node", node}, {"series", series}, {"x", at}, {"y", y}};
  if (!unit.empty()) msg["unit"] = unit;
  send_event(std::move(msg));
}

void Run::monitor_state(std::string_view node, toolkit::MonitorState state, double value, std::string_view port) {
  if (node.empty()) return;
  const std::string name(toolkit::monitor_state_name(state));
  const std::string key = std::string(node) + "\n" + std::string(port);
  const auto said = monitor_said_.find(key);
  // Every monitor starts at ok: only a change is news.
  if (said == monitor_said_.end() ? state == toolkit::MonitorState::Ok : said->second == name) return;
  monitor_said_.insert_or_assign(key, name);
  json msg = {{"type", "monitor.state"}, {"node", node}, {"state", name}, {"t", sample_time(ctx_.t)}};
  if (std::isfinite(value)) msg["v"] = value;
  if (!port.empty()) msg["port"] = port;
  send_event(std::move(msg));
}

void Run::frame_of(std::string_view node, const toolkit::Frame& frame) {
  if (node.empty()) return;
  auto fields = frame_fields(frame);
  if (const auto* why = std::get_if<std::string>(&fields)) return warn_once(node, "frame:" + *why, "frame not shown: " + *why);
  json msg = {{"type", "frame"}, {"node", node}, {"t", sample_time(ctx_.t)}};
  msg.update(std::get<json>(fields));
  if (recording_ && node == recording_->node) {  // what a reused block shows again: its last picture
    std::erase_if(recording_->events, [](const json& m) { return m["type"] == "frame"; });
    recording_->events.push_back(msg);
  }
  const auto now = std::chrono::steady_clock::now();
  const auto last = frame_sent_.find(node);
  if (last != frame_sent_.end() && now - last->second < std::chrono::milliseconds(50)) {
    frames_.insert_or_assign(std::string(node), std::move(msg));  // the latest waits for the next flush
    return;
  }
  frame_sent_.insert_or_assign(std::string(node), now);
  frames_.erase(std::string(node));
  emit_(msg);
}

void Run::flush_frames(bool all) {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = frames_.begin(); it != frames_.end();) {
    auto& sent = frame_sent_[it->first];
    if (!all && now - sent < std::chrono::milliseconds(50)) {
      ++it;
      continue;
    }
    sent = now;
    emit_(it->second);
    it = frames_.erase(it);
  }
}

void Run::at_time(double t) {
  ctx_.t = t;
  if (keys_now_) keys_ = keys_now_();
  if (tune_now_)
    if (const auto list = tune_now_()) tune(*list);
}

// Tunable variables (#75). Only a variable the run started with, marked `tunable`, and not a
// formula, changes: it takes its latest value from `list` (a different type, or a value its
// range refuses, is ignored). Formulas over it follow. Then every number param bound to a
// variable that came to something else is changed on its block, which says whether it can.
void Run::tune(const json& list) {
  if (!list.is_array() || !variables_given_.is_array()) return;
  json next = variables_given_;
  for (auto& v : next) {
    if (!v.value("tunable", false) || v.contains("expr")) continue;
    const auto sent = std::ranges::find_if(list, [&](const json& x) { return x.value("name", std::string{}) == v.value("name", std::string{}); });
    if (sent != list.end() && !sent->contains("expr") && sent->contains("value") && sent->value("type", std::string{}) == v.value("type", std::string{}))
      v["value"] = sent->at("value");
  }
  apply_variables(next);
}

void Run::apply_variables(const json& next) {
  if (next == variables_given_) return;
  try {
    const toolkit::Variables now(next);
    if (const auto* nodes = graph_given_.is_object() && graph_given_.contains("nodes") ? &graph_given_.at("nodes") : nullptr) {
      for (const auto& n : *nodes) {
        const auto found = g_.nodes.find(n.value("id", std::string{}));
        if (found == g_.nodes.end() || !n.contains("params") || !n["params"].is_object()) continue;
        Node& node = found->second;
        for (const auto& [param, given] : n["params"].items()) {
          const auto def = std::ranges::find(node.entry->def.params, param, &toolkit::ParamDef::id);
          if (!toolkit::is_reference(given) || def == node.entry->def.params.end() || !def->is_number()) continue;
          const json value = now.value_for(given, *def, node.id);
          if (value != vars_.value_for(given, *def, node.id)) change_param(node, *def, value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>());
        }
      }
    }
    if (station_given_.is_object() && station_given_.contains("objects")) {
      for (const auto& o : station_given_.at("objects")) {
        const auto* entry = backend_.catalog->find_object(o.value("type", std::string{}));
        if (!entry || !o.contains("props") || !o["props"].is_object()) continue;
        const auto id = o.value("id", std::string{});
        for (const auto& [prop, given] : o["props"].items()) {
          const auto def = std::ranges::find(entry->def.props, prop, &toolkit::ParamDef::id);
          if (!toolkit::is_reference(given) || def == entry->def.props.end()) continue;
          if (now.value_for(given, *def, id) != vars_.value_for(given, *def, id) && told_.insert(id + "." + prop).second)
            log("warn", id + "." + prop + " changes with the next run: a station object takes its properties when it is made");
        }
      }
    }
    vars_ = now;
    variables_given_ = next;
    live_variables_ = variable_values(vars_);
  } catch (const toolkit::VariableError& err) {
    log("warn", std::string("variable change ignored: ") + err.what());
  }
}

// A block's write (#166). Only what the UI may tune: a variable of the run, marked `tunable`,
// not a formula, of the value's type and within its range. A refusal warns once per variable
// and reason, so a block that writes every step doesn't flood the Console.
bool Run::set_variable(std::string_view name, const toolkit::Value& value) {
  const std::string who(name);
  const auto refuse = [&](const std::string& why) {
    if (told_.insert("set:" + who + ":" + why).second) log_(ctx_.node, "warn", "set_variable(\"" + who + "\"): " + why);
    return false;
  };
  if (!variables_given_.is_array()) return refuse("no such variable");
  json next = pending_.is_null() ? variables_given_ : pending_;
  const auto v = std::ranges::find_if(next, [&](const json& x) { return x.value("name", std::string{}) == who; });
  if (v == next.end()) return refuse("no such variable");
  if (v->contains("expr")) return refuse("a formula can't be set; set the variables it uses");
  if (!v->value("tunable", false)) return refuse("not tunable");
  const std::string type = v->value("type", std::string("number"));
  if (const double* number = std::get_if<double>(&value)) {
    if (type == "number") (*v)["value"] = *number;
    else if (type == "boolean") (*v)["value"] = *number != 0;
    else return refuse("is " + type + ", not a number");
  } else if (const auto* text = std::get_if<std::string>(&value)) {
    if (type != "enum" && type != "text" && type != "file") return refuse("is " + type + ", not text");
    (*v)["value"] = *text;
  } else {
    return refuse("takes a number or text");
  }
  try {
    const toolkit::Variables check(next);  // range, whole, enum options
  } catch (const toolkit::VariableError& err) {
    return refuse(err.what());
  }
  pending_ = std::move(next);
  return true;
}

void Run::commit_variables() {
  if (pending_.is_null()) return;
  const json next = std::move(pending_);
  pending_ = nullptr;
  const json before = vars_.values();
  apply_variables(next);
  const json after = vars_.values();
  json changed = json::object();
  for (const auto& [name, v] : after.items())
    if (!before.contains(name) || before.at(name) != v) changed[name] = v;
  if (!changed.empty()) emit_({{"type", "variables.patch"}, {"values", changed}});
}

// One block's number param to `value`: by the block, else (a block with no state) by making it
// again. Otherwise the user is told once that it waits for the next run.
bool Run::change_param(Node& node, const toolkit::ParamDef& def, double value) {
  auto params = node.params;
  params[def.id] = value;
  if (node.block) {
    if (node.block->set_param(def.id, value)) {
      node.params = std::move(params);
      return true;
    }
    if (node.block->stateless()) {
      try {
        node.block = node.entry->make(params, node.text);
        node.params = std::move(params);
        return true;
      } catch (const std::exception& err) {
        log("warn", node.id + "." + def.id + ": not changed: " + err.what());
        return false;
      }
    }
  }
  if (told_.insert(node.id + "." + def.id).second)
    log("warn", node.id + "." + def.id + " changes with the next run: " + node.def.label + " can't take it during a run");
  return false;
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

json Run::pass() {
  json samples = pass_over(g_.order);
  commit_variables();  // between passes, so every block saw the same variables this one
  return samples;
}

json Run::pass_over(const std::vector<std::string>& order) {
  json samples = json::array();
  // What an instrument shows, `in` of `shown`, as `signal` samples. `port` is the output it
  // came from, for a vector's component names.
  // `input` names the monitor's input it came in by, for one with several (`sample_per_input`); "" is `in`.
  const auto sample = [&](const Node& node, const toolkit::Values& shown, const toolkit::PortDef* port, const std::string& input = {}) {
    const double t = std::round(ctx_.t * 1e4) / 1e4;
    const auto first = samples.size();
    if (const auto* text = toolkit::get_if<std::string>(shown, "in")) {
      samples.push_back({{"type", "signal"}, {"node", node.id}, {"t", t}, {"v", *text}});
    } else if (const auto* vector = toolkit::get_vector(shown, "in")) {
      if (node.def.whole_vector || settings_.mode == RunSettings::Mode::Clock) {
        // A readout or scope shows the whole vector, and a graph in a clock run draws one trace per component
        // over time: the vector is one sample. A Graph in a run-once plots it by index (x is
        // the index), useful for point series.
        json item = {{"type", "signal"}, {"node", node.id}, {"t", t}, {"v", *vector}};
        if (port != nullptr && !port->components.empty()) {
          json components = json::array();
          for (const auto& component : port->components) {
            json c = {{"id", component.id}};
            if (!component.unit.empty()) c["unit"] = component.unit;
            components.push_back(std::move(c));
          }
          item["components"] = std::move(components);
        }
        samples.push_back(std::move(item));
      } else {
        for (std::size_t i = 0; i < vector->size(); ++i)
          samples.push_back({{"type", "signal"}, {"node", node.id}, {"t", i}, {"v", (*vector)[i]}});
      }
    } else {
      samples.push_back({{"type", "signal"}, {"node", node.id}, {"t", t}, {"v", toolkit::get_or(shown, "in")}});
    }
    if (!input.empty())
      for (auto i = first; i < samples.size(); ++i) samples[i]["port"] = input;
  };
  // The output port wired into `node`'s input `input`, for a vector's component names.
  const auto source_port = [&](const std::string& id, const std::string& input) -> const toolkit::PortDef* {
    const auto wire = g_.wires.find({id, input});
    if (wire == g_.wires.end()) return nullptr;
    const auto& source = g_.nodes.at(wire->second.first);
    const auto it = std::ranges::find(source.def.outputs, wire->second.second, &toolkit::PortDef::id);
    return it == source.def.outputs.end() ? nullptr : &*it;
  };
  for (const auto& id : order) {
    auto& node = g_.nodes.at(id);
    const auto call = calling(id);
    if (node.commented && node.comment_mode == "through") {
      toolkit::Values in = inputs_of(node);
      toolkit::Values out;
      for (const auto& out_p : node.def.outputs) {
        if (auto it = in.find(out_p.id); it != in.end()) {
          out[out_p.id] = it->second;
        } else if (out_p.id == "out" && in.contains("in")) {
          out["out"] = in.at("in");
        } else if (node.def.inputs.size() == 1 && node.def.outputs.size() == 1) {
          if (!in.empty()) {
            out[out_p.id] = in.begin()->second;
          }
        }
      }
      outputs_[id] = std::move(out);
      validate_vector_outputs(node, outputs_[id]);
      continue;
    }
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
      if (node.def.sample_per_input) {  // one sample per wired input, tagged with it
        for (const auto& p : node.def.inputs)
          if (const auto v = in.find(p.id); v != in.end()) sample(node, {{"in", v->second}}, source_port(id, p.id), p.id);
        continue;
      }
      if (node.monitor)
        if (auto shown = node.monitor->step(ctx_, in); shown.contains("value")) in["in"] = shown.at("value");
      sample(node, in, source_port(id, "in"));
      continue;
    }
    // A block with an instrument and outputs (a Switch) shows its first output.
    if (!node.def.instrument.empty() && !node.def.outputs.empty()) {
      const auto& port = node.def.outputs.front();
      toolkit::Values shown;
      if (const auto it = outputs_[id].find(port.id); it != outputs_[id].end()) shown["in"] = it->second;
      sample(node, shown, &port);
    }
  }
  // Blocks without feedthrough take this pass's inputs now that every block has run.
  for (const auto& id : order) {
    auto& node = g_.nodes.at(id);
    if (!node.block || node.def.feedthrough || node.commented) continue;
    const auto call = calling(id);
    node.block->step(ctx_, inputs_of(node));
  }
  return samples;
}

std::optional<std::string> Run::cache_signature(const Node& node) const {
  if (!cache_ || !node.block || node.object || node.commented || !node.def.feedthrough) return std::nullopt;
  if (!node.def.cacheable.value_or(node.block->stateless())) return std::nullopt;
  json params = json::object();
  for (const auto& [k, v] : node.params) params[k] = signature_value(v);
  json files = json::object();  // a file param's file may change between runs: its time and size
  for (const auto& p : node.def.params) {
    if (p.type != "file") continue;
    const auto text = node.text.find(p.id);
    if (text == node.text.end() || text->second.empty()) continue;
    std::filesystem::path path(text->second);
    if (path.is_relative()) path = backend_.catalog->dir / path;
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    const auto size = ec ? 0 : std::filesystem::file_size(path, ec);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    files[p.id] = ec ? json("missing") : json{static_cast<std::int64_t>(ns), static_cast<std::uint64_t>(size)};
  }
  const auto& o = settings_.solver_options;
  const json signature = {
      {"catalog", reinterpret_cast<std::uintptr_t>(backend_.catalog)},
      {"type", node.def.type},
      {"template", node.entry && node.entry->from ? node.entry->from->name : ""},
      {"inputs", ports_json(node.def.inputs)},
      {"outputs", ports_json(node.def.outputs)},
      {"params", params},
      {"text", node.text},
      {"files", files},
      {"lanes", node.lanes ? json(*node.lanes) : json()},
      {"t", signature_value(ctx_.t)},
      {"dt", signature_value(ctx_.dt)},
      {"solver", {settings_.solver, signature_value(o.rtol), signature_value(o.atol), signature_value(o.min_step),
                  signature_value(o.max_step), o.max_steps, o.sub_steps}},
  };
  return signature.dump();
}

json Run::evaluate(const std::string& id) {
  evaluated_ = true;
  auto& node = g_.nodes.at(id);
  std::optional<std::string> signature;
  toolkit::Values inputs;
  std::shared_ptr<const RunCache::Entry> hit;
  {
    const auto call = calling(id);
    signature = cache_signature(node);
    if (signature) {
      inputs = inputs_of(node);
      hit = cache_->find(id, *signature, inputs, live_variables_);
    }
  }
  if (hit) {  // what it gave last time, through the pass like any block (its instrument shows it)
    node.block = std::make_unique<Reused>(hit);
    node_state(id, "cached");  // with no running before it: it isn't called
    for (const auto& [level, text] : hit->logs) log_(id, level, text);
    for (const auto& event : hit->events) {  // its metrics, monitor states and picture, as when it ran
      if (event["type"] == "monitor.state")
        monitor_said_.insert_or_assign(id + "\n" + event.value("port", std::string{}), event["state"].get<std::string>());
      emit_(event);
    }
    ++reused_;
    return pass_over({id});
  }
  node_state(id, "running");
  if (signature) recording_ = Recording{.node = id, .variables = {}, .logs = {}, .set_variable = false};
  json samples;
  try {
    samples = pass_over({id});
  } catch (...) {
    recording_.reset();  // a block that failed keeps what it had (a result for other inputs)
    throw;
  }
  const auto recorded = std::exchange(recording_, std::nullopt);
  // Kept unless it did more than give outputs (set a variable), or the run was cancelled before
  // it was done: a block cut short may have given less than its result.
  if (recorded && recorded->set_variable) {
    cache_->erase(id);
  } else if (recorded && !stop_.stop_requested()) {
    cache_->store(id, {.signature = std::move(*signature),
                       .inputs = std::move(inputs),
                       .variables = recorded->variables,
                       .outputs = outputs_[id],
                       .entities = node.block->entities(),
                       .logs = recorded->logs,
                       .events = recorded->events});
  }
  flush_frames(true);  // its last picture before it is done
  node_state(id, "done");
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
    const bool passive = linked != g_.nodes.end() && linked->second.commented && linked->second.comment_mode == "out";
    item.instance->step(ctx_, linked != g_.nodes.end() && linked->second.object && !passive ? inputs_of(linked->second) : toolkit::Values{});
  }
  return state_ ? state_->step(ctx_) : std::nullopt;
}

void Run::frame(const json& samples, std::optional<double> progress) {
  for (const auto& s : samples) emit_(s);
  for (const auto& [node, list] : story_samples_)
    for (const auto& sample : list) emit_(sample);
  story_samples_.clear();
  flush_frames(false);
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
      if (lag > 0.2 && (now - last_lag_warn_ > std::chrono::seconds(2))) {
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
    if (node.block && !(node.commented && node.comment_mode == "out")) out.push_back(node.block.get());
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
  if (settings.contains("solver")) {
    if (!settings.at("solver").is_string())
      throw std::invalid_argument("run setting solver must be a string");
    const std::string s = settings.at("solver").get<std::string>();
    const auto& valid = toolkit::solve::names();
    if (std::find(valid.begin(), valid.end(), s) == valid.end()) {
      std::string list;
      for (const auto& name : valid) list += (list.empty() ? "" : ", ") + name;
      throw std::invalid_argument("unknown solver \"" + s + "\" (expected one of: " + list + ")");
    }
    base.solver = s;
  }
  if (settings.contains("solver_options")) {
    const auto& o = settings.at("solver_options");
    if (!o.is_object()) throw std::invalid_argument("run setting solver_options must be an object");
    auto& into = base.solver_options;
    for (const auto& [key, v] : o.items()) {
      const bool whole = key == "max_steps" || key == "sub_steps";
      if (!v.is_number() || (whole && !v.is_number_integer()))
        throw std::invalid_argument("solver option " + key + " must be " + (whole ? "a whole number" : "a number"));
      if (key == "rtol") into.rtol = v.get<double>();
      else if (key == "atol") into.atol = v.get<double>();
      else if (key == "min_step") into.min_step = v.get<double>();
      else if (key == "max_step") into.max_step = v.get<double>();
      else if (key == "max_steps") into.max_steps = v.get<int>();
      else if (key == "sub_steps") into.sub_steps = v.get<int>();
      else throw std::invalid_argument("no solver option " + key + " (rtol, atol, min_step, max_step, max_steps, sub_steps)");
    }
    toolkit::solve::check(into);
  }
  if (base.duration > 0 && base.duration / base.dt > 1e7)
    throw std::invalid_argument("run settings: more than 10 million clock steps (duration / dt)");
  return base;
}

json settings_json(const RunSettings& s) {
  json j = {{"mode", s.mode == RunSettings::Mode::Once ? "once" : "clock"}, {"duration", s.duration}, {"dt", s.dt}, {"speed", s.speed}};
  if (!s.solver.empty()) {
    j["solver"] = s.solver;
  }
  const auto& o = s.solver_options;
  j["solver_options"] = {{"rtol", o.rtol},         {"atol", o.atol},           {"min_step", o.min_step},
                         {"max_step", o.max_step}, {"max_steps", o.max_steps}, {"sub_steps", o.sub_steps}};
  return j;
}

std::string run(const std::string& id, const json& graph, const json& station, const Backend& backend,
                const Emit& emit, const KeysNow& keys, const std::stop_token& stop, const json& settings,
                const json& variables, const VariablesNow& tune, RunCache* cache) {
  RunSettings s;
  try {
    s = with_overrides(backend.run, settings);
  } catch (const std::invalid_argument& err) {
    emit({{"type", "run.started"}, {"run", id}});
    emit(log_msg("error", err.what()));
    emit({{"type", "run.finished"}, {"run", id}, {"status", "failed"}});
    return "failed";
  }
  toolkit::solve::set_defaults({s.solver, s.solver_options});  // this thread's "auto" solvers
  return Run(id, backend, s, emit, keys, stop, tune, cache).go(graph, station, variables);
}

}  // namespace paperboat::core
