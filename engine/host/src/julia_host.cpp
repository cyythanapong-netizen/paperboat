// A backend whose domain code lives in another runtime (Julia): the engine reads a JSON
// description of the block and station object types, then raises events the host answers on
// its own thread. The engine runs on its worker threads; every call crosses to the host
// through the pump below, so Julia is never entered from an engine thread.
// No graph, solver or protocol code is here; those stay in the engine.

#include "paperboat/host/host.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/model.hpp"
#include "paperboat/toolkit/registry.hpp"
#include "paperboat/toolkit/station.hpp"

namespace {

using nlohmann::json;
namespace tk = paperboat::toolkit;
namespace core = paperboat::core;

std::string g_last_error;

json parse_reply(const std::string& text) {
  json out = json::parse(text, nullptr, false);
  if (out.is_discarded()) throw std::runtime_error("host: a reply is not JSON");
  if (out.is_object() && out.contains("error")) throw std::runtime_error(out["error"].get<std::string>());
  return out;
}

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// --- JSON <-> the engine's Values ---------------------------------------------------------

json value_json(const tk::Value& value) {
  if (const auto* number = std::get_if<double>(&value)) return *number;
  if (const auto* vector = std::get_if<tk::Vector>(&value)) return *vector;
  if (const auto* table = std::get_if<tk::Table>(&value)) return *table;
  if (const auto* text = std::get_if<std::string>(&value)) return *text;
  throw std::runtime_error("a handle cannot travel to the host");
}

json values_json(const tk::Values& values) {
  json out = json::object();
  for (const auto& [id, value] : values) out[id] = value_json(value);
  return out;
}

tk::Value value_from_json(const json& value) {
  if (value.is_number()) return value.get<double>();
  if (value.is_boolean()) return value.get<bool>() ? 1.0 : 0.0;
  if (value.is_string()) return value.get<std::string>();
  if (value.is_array()) {
    if (!value.empty() && value.front().is_array()) return value.get<tk::Table>();
    return value.get<tk::Vector>();
  }
  throw std::runtime_error("a value must be a number, text, a list or a table");
}

tk::Values values_from_json(const json& object) {
  tk::Values out;
  if (!object.is_object()) return out;
  for (const auto& [id, value] : object.items()) out[id] = value_from_json(value);
  return out;
}

tk::Entity entity_from_json(const json& item) {
  tk::Entity entity;
  entity.id = item.value("id", "");
  entity.shape = item.value("shape", "box");
  if (item.contains("pos")) entity.pos = item["pos"].get<std::array<double, 3>>();
  if (item.contains("size")) entity.size = item["size"].get<std::array<double, 3>>();
  if (item.contains("rot")) entity.rot = item["rot"].get<std::array<double, 3>>();
  if (item.contains("to")) entity.to = item["to"].get<std::array<double, 3>>();
  entity.color = item.value("color", "#888888");
  entity.src = item.value("src", "");
  if (item.contains("vertices")) entity.vertices = item["vertices"].get<std::vector<double>>();
  if (item.contains("indices")) entity.indices = item["indices"].get<std::vector<double>>();
  if (item.contains("values")) entity.values = item["values"].get<std::vector<double>>();
  return entity;
}

std::vector<tk::Entity> entities_from_json(const json& list) {
  std::vector<tk::Entity> out;
  if (!list.is_array()) return out;
  for (const auto& item : list) out.push_back(entity_from_json(item));
  return out;
}

// --- The description ----------------------------------------------------------------------

tk::PortDef port_from_json(const json& item) {
  tk::PortDef port;
  port.id = item.value("id", "");
  port.type = item.value("type", "signal");
  port.unit = item.value("unit", "");
  if (item.contains("dimension") && !item["dimension"].is_null())
    port.dimension = item["dimension"].get<std::size_t>();
  if (item.contains("components"))
    for (const auto& component : item["components"])
      port.components.push_back({component.value("id", ""), component.value("unit", "")});
  port.broadcast = item.value("broadcast", false);
  return port;
}

tk::ParamDef param_from_json(const json& item) {
  tk::ParamDef param;
  param.id = item.value("id", "");
  param.default_value = item.value("default", 0.0);
  param.type = item.value("type", "number");
  param.default_text = item.value("text", "");
  if (item.contains("options")) param.options = item["options"].get<std::vector<std::string>>();
  param.unit = item.value("unit", "");
  return param;
}

tk::Kind kind_from_json(const std::string& name) {
  if (name == "source") return tk::Kind::Source;
  if (name == "probe") return tk::Kind::Probe;
  if (name == "monitor") return tk::Kind::Monitor;
  return tk::Kind::Function;
}

tk::BlockDef block_def_from_json(const json& item) {
  tk::BlockDef def;
  def.type = item.value("type", "");
  if (def.type.empty()) throw std::runtime_error("a block definition needs \"type\"");
  def.label = item.value("label", def.type);
  def.kind = kind_from_json(item.value("kind", "function"));
  if (item.contains("inputs")) for (const auto& port : item["inputs"]) def.inputs.push_back(port_from_json(port));
  if (item.contains("outputs")) for (const auto& port : item["outputs"]) def.outputs.push_back(port_from_json(port));
  if (item.contains("params")) for (const auto& param : item["params"]) def.params.push_back(param_from_json(param));
  def.feedthrough = item.value("feedthrough", true);
  def.elementwise = item.value("elementwise", false);
  def.description = item.value("description", "");
  if (item.contains("flows")) def.flows = item["flows"].get<std::vector<std::string>>();
  if (def.kind == tk::Kind::Monitor) def.instrument = item.value("instrument", "graph");
  return def;
}

tk::ObjectDef object_def_from_json(const json& item) {
  tk::ObjectDef def;
  def.type = item.value("type", "");
  if (def.type.empty()) throw std::runtime_error("a station object definition needs \"type\"");
  def.label = item.value("label", def.type);
  def.role = item.value("role", "body");
  if (item.contains("props")) for (const auto& prop : item["props"]) def.props.push_back(param_from_json(prop));
  if (item.contains("links")) def.links = item["links"].get<std::vector<std::string>>();
  def.description = item.value("description", "");
  if (item.contains("block") && !item["block"].is_null()) def.block = block_def_from_json(item["block"]);
  return def;
}

// --- The pump ------------------------------------------------------------------------------

struct Event {
  long long seq = 0;
  int id = 0;
  std::string name;
  std::string payload;
};

struct Pending {
  std::mutex mutex;
  std::condition_variable ready;
  std::string reply;
  bool done = false;
};

struct Runtime {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Event> queue;
  std::map<long long, std::shared_ptr<Pending>> pending;
  std::atomic<long long> seq{0};
  std::atomic<int> ids{1};
  std::atomic<bool> stopping{false};
  std::atomic<bool> failed{false};
  std::string error;

  // The engine's objects outlive every run: the handle owns them.
  tk::Registry registry;
  tk::Catalog catalog;
  core::Backend backend;
  std::unique_ptr<core::Server> server;
  std::thread runner;

  std::string invoke(int id, const char* event, const json& payload) {
    auto slot = std::make_shared<Pending>();
    Event request;
    request.seq = ++seq;
    request.id = id;
    request.name = event;
    request.payload = payload.dump();
    {
      std::lock_guard lock(mutex);
      pending[request.seq] = slot;
      queue.push_back(std::move(request));
    }
    cv.notify_one();
    std::unique_lock lock(slot->mutex);
    slot->ready.wait(lock, [&] { return slot->done || stopping.load(); });
    if (!slot->done) throw std::runtime_error("host stopped before the event was answered");
    return slot->reply;
  }

  json instance(int id, const tk::Values& params, const tk::Config& config,
                const json& extra = json::object()) {
    json payload = extra;
    payload["params"] = json::object();
    for (const auto& [key, value] : params) payload["params"][key] = value_json(value);
    for (const auto& [key, value] : config) payload["params"][key] = value;
    return parse_reply(invoke(id, "new", payload));
  }

  void finish() {
    stopping = true;
    cv.notify_all();
    std::lock_guard lock(mutex);
    for (auto& [number, slot] : pending) {
      std::lock_guard wait(slot->mutex);
      slot->done = true;
    }
  }
};

json inputs_payload(const tk::Context& ctx, const tk::Values& in) {
  json payload = {{"t", ctx.t}, {"dt", ctx.dt}, {"inputs", values_json(in)}};
  if (ctx.keys_down) payload["keys"] = *ctx.keys_down;
  return payload;
}

class HostBlock final : public tk::Block {
 public:
  HostBlock(Runtime& runtime, int id) : runtime_(runtime), id_(id) {}

  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    return values_from_json(parse_reply(runtime_.invoke(id_, "step", inputs_payload(ctx, in)))
                                .value("outputs", json::object()));
  }
  std::vector<tk::Entity> entities() const override {
    return entities_from_json(
        parse_reply(runtime_.invoke(id_, "entities", json::object())).value("entities", json::array()));
  }

 private:
  Runtime& runtime_;
  int id_;
};

class HostGenerator final : public tk::Block {
 public:
  HostGenerator(Runtime& runtime, int id, std::string output)
      : runtime_(runtime), id_(id), output_(std::move(output)) {}

  tk::Values step(const tk::Context& ctx, const tk::Values&) override {
    const json out = parse_reply(runtime_.invoke(id_, "value", {{"t", ctx.t}, {"dt", ctx.dt}}));
    return {{output_, value_from_json(out.value("value", json(0.0)))}};
  }
  std::vector<tk::Entity> entities() const override {
    return entities_from_json(
        parse_reply(runtime_.invoke(id_, "entities", json::object())).value("entities", json::array()));
  }

 private:
  Runtime& runtime_;
  int id_;
  std::string output_;
};

class HostStep final : public tk::Step {
 public:
  HostStep(Runtime& runtime, int id) : runtime_(runtime), id_(id) {}

  tk::Values outputs() const override {
    return values_from_json(parse_reply(runtime_.invoke(id_, "outputs", json::object()))
                                .value("outputs", json::object()));
  }
  void enter(const tk::Context& ctx, const tk::Values& in) override {
    runtime_.invoke(id_, "enter", inputs_payload(ctx, in));
  }
  std::string tick(const tk::Context& ctx, const tk::Values& in) override {
    return parse_reply(runtime_.invoke(id_, "tick", inputs_payload(ctx, in))).value("port", "");
  }
  bool may_start() const override {
    return parse_reply(runtime_.invoke(id_, "may_start", json::object())).value("start", false);
  }
  bool ends_run() const override {
    return parse_reply(runtime_.invoke(id_, "ends_run", json::object())).value("end", false);
  }
  bool restarts() const override {
    return parse_reply(runtime_.invoke(id_, "restarts", json::object())).value("restart", false);
  }
  bool shared() const override {
    return parse_reply(runtime_.invoke(id_, "shared", json::object())).value("shared", false);
  }

 private:
  Runtime& runtime_;
  int id_;
};

class HostObject final : public tk::StationObject {
 public:
  HostObject(Runtime& runtime, int id) : runtime_(runtime), id_(id) {}

  tk::Values read() const override {
    return values_from_json(parse_reply(runtime_.invoke(id_, "read", json::object()))
                                .value("outputs", json::object()));
  }
  void step(const tk::Context& ctx, const tk::Values& in) override {
    runtime_.invoke(id_, "step", inputs_payload(ctx, in));
  }
  std::vector<tk::Entity> entities(const std::string& id, double z) const override {
    return entities_from_json(parse_reply(runtime_.invoke(id_, "entities", {{"id", id}, {"z", z}}))
                                  .value("entities", json::array()));
  }
  void link(std::string_view slot, tk::StationObject&) override {
    runtime_.invoke(id_, "link", {{"slot", std::string(slot)}});
  }

 private:
  Runtime& runtime_;
  int id_;
};

class HostModel final : public tk::Model {
 public:
  HostModel(Runtime& runtime, int id, tk::solve::State initial, std::string_view solver)
      : tk::Model(std::move(initial), solver), runtime_(runtime), id_(id) {}

  tk::Values read() const override {
    return values_from_json(parse_reply(runtime_.invoke(id_, "read", json::object()))
                                .value("outputs", json::object()));
  }
  tk::solve::State derivatives(double t, const tk::solve::State& x, const tk::Values& in) const override {
    const json out = parse_reply(runtime_.invoke(id_, "derivatives", {{"t", t}, {"x", x}, {"inputs", values_json(in)}}));
    return out.value("dx", tk::solve::State{});
  }
  std::vector<tk::Entity> entities(const std::string& id, double /*z*/) const override {
    return entities_from_json(
        parse_reply(runtime_.invoke(id_, "entities", {{"id", id}})).value("entities", json::array()));
  }

 private:
  Runtime& runtime_;
  int id_;
};

class HostState final : public tk::DomainState {
 public:
  explicit HostState(Runtime& runtime, int id) : runtime_(runtime), id_(id) {}
  std::vector<tk::Entity> entities() const override {
    return entities_from_json(
        parse_reply(runtime_.invoke(id_, "state_entities", json::object())).value("entities", json::array()));
  }
  std::optional<std::string> step(const tk::Context& ctx) override {
    const json out = parse_reply(runtime_.invoke(id_, "state_step", {{"t", ctx.t}, {"dt", ctx.dt}}));
    if (out.contains("end") && out["end"].is_string()) return out["end"].get<std::string>();
    return std::nullopt;
  }
  std::optional<std::string> summary() const override {
    const json out = parse_reply(runtime_.invoke(id_, "summary", json::object()));
    if (out.contains("summary") && out["summary"].is_string()) return out["summary"].get<std::string>();
    return std::nullopt;
  }

 private:
  Runtime& runtime_;
  int id_;
};

// --- Building the registry ----------------------------------------------------------------

void add_block(tk::Registry& registry, Runtime& runtime, const std::string& domain, const json& item) {
  tk::Template t;
  t.def = block_def_from_json(item);
  t.name = domain + "/" + t.def.type;
  const std::string base = item.value("base", "block");
  const std::string type = t.def.type;
  if (base == "step") {
    t.make_step = [&runtime, type](const tk::Values& params, const tk::Config& config) -> std::unique_ptr<tk::Step> {
      const int id = runtime.ids.fetch_add(1);
      runtime.instance(id, params, config, {{"type", type}, {"base", "step"}});
      return std::make_unique<HostStep>(runtime, id);
    };
  } else if (base == "generator") {
    const std::string output = t.def.outputs.empty() ? "y" : t.def.outputs.front().id;
    t.make = [&runtime, output, type](const tk::Values& params, const tk::Config& config) -> std::unique_ptr<tk::Block> {
      const int id = runtime.ids.fetch_add(1);
      runtime.instance(id, params, config, {{"type", type}, {"base", "generator"}});
      return std::make_unique<HostGenerator>(runtime, id, output);
    };
  } else {
    t.make = [&runtime, type](const tk::Values& params, const tk::Config& config) -> std::unique_ptr<tk::Block> {
      const int id = runtime.ids.fetch_add(1);
      runtime.instance(id, params, config, {{"type", type}, {"base", "block"}});
      return std::make_unique<HostBlock>(runtime, id);
    };
  }
  registry.add(std::move(t));
}

void add_object(tk::Registry& registry, Runtime& runtime, const std::string& domain, const json& item,
                const std::string& backend_solver) {
  tk::StationTemplate t;
  t.def = object_def_from_json(item);
  t.name = domain + "/" + t.def.type;
  const std::string base = item.value("base", "object");
  const std::string type = t.def.type;
  if (base == "model") {
    const std::string fallback = item.value("solver", backend_solver);
    t.make = [&runtime, fallback, type](const tk::Values& props) -> std::unique_ptr<tk::StationObject> {
      const int id = runtime.ids.fetch_add(1);
      const json created = runtime.instance(id, props, {}, {{"type", type}, {"base", "model"}});
      tk::solve::State initial;
      if (created.contains("state")) initial = created["state"].get<tk::solve::State>();
      const std::string solver = tk::get_text(props, "solver", fallback);
      return std::make_unique<HostModel>(runtime, id, std::move(initial), solver);
    };
  } else {
    t.make = [&runtime, type](const tk::Values& props) -> std::unique_ptr<tk::StationObject> {
      const int id = runtime.ids.fetch_add(1);
      runtime.instance(id, props, {}, {{"type", type}, {"base", "object"}});
      return std::make_unique<HostObject>(runtime, id);
    };
  }
  registry.add(std::move(t));
}

}  // namespace

extern "C" {

pb_host* pb_host_new(const char* config_text, int run_only) {
  g_last_error.clear();
  try {
    if (!config_text) throw std::runtime_error("no config");
    const json config = json::parse(config_text, nullptr, false);
    if (config.is_discarded()) throw std::runtime_error("the config is not JSON");

    const std::string descriptor = config.value("descriptor", "");
    if (descriptor.empty()) throw std::runtime_error("the config needs \"descriptor\"");
    const std::string domain = tk::descriptor_domain(read_text(descriptor));

    auto runtime = std::make_unique<Runtime>();
    runtime->registry = tk::builtins();
    if (config.contains("blocks")) {
      if (domain.empty()) throw std::runtime_error(descriptor + ": needs a `domain` when the host defines blocks");
      for (const auto& item : config["blocks"]) add_block(runtime->registry, *runtime, domain, item);
    }
    const std::string solver = config.value("solver", "rk4");
    if (config.contains("objects")) {
      if (domain.empty()) throw std::runtime_error(descriptor + ": needs a `domain` when the host defines objects");
      for (const auto& item : config["objects"]) add_object(runtime->registry, *runtime, domain, item, solver);
    }

    runtime->catalog = tk::load_descriptor_file(descriptor, runtime->registry);
    runtime->backend.engine = config.value("engine", "julia");
    runtime->backend.catalog = &runtime->catalog;
    runtime->backend.view = config.contains("view") ? config["view"] : json(nullptr);
    runtime->backend.initial_project = config.contains("project") ? config["project"] : json(nullptr);
    runtime->backend.initial_project_name = config.value("project_name", "");
    runtime->backend.run = {
        .mode = config.value("mode", "clock") == "once" ? core::RunSettings::Mode::Once
                                                        : core::RunSettings::Mode::Clock,
        .dt = config.value("dt", 0.01),
        .duration = config.value("duration", 10.0),
        .emit_every = config.value("emit_every", 4),
        .real_time = config.value("real_time", true),
        .speed = config.value("speed", 1.0),
        .solver = solver,
    };

    if (config.value("state", false)) {
      const int id = runtime->ids.fetch_add(1);
      runtime->invoke(id, "new", json::object());
      runtime->backend.state = [runtime = runtime.get(), id]() -> std::unique_ptr<tk::DomainState> {
        return std::make_unique<HostState>(*runtime, id);
      };
    }

    if (run_only) {
      runtime->runner = std::thread([runtime = runtime.get()] {
        const std::string status = core::run_starter(runtime->backend, std::cout);
        if (status != "done") runtime->failed = true;
        runtime->finish();
      });
    } else {
      runtime->server = std::make_unique<core::Server>(runtime->backend, config.value("port", 0));
      const std::string error = runtime->server->start();
      if (!error.empty()) throw std::runtime_error("cannot listen: " + error);
    }
    return reinterpret_cast<pb_host*>(runtime.release());
  } catch (const std::exception& error) {
    g_last_error = error.what();
    return nullptr;
  }
}

const char* pb_host_next(pb_host* host) {
  if (!host) return nullptr;
  Runtime& runtime = *reinterpret_cast<Runtime*>(host);
  std::unique_lock lock(runtime.mutex);
  runtime.cv.wait(lock, [&] { return !runtime.queue.empty() || runtime.stopping.load(); });
  if (runtime.queue.empty()) return nullptr;
  Event event = std::move(runtime.queue.front());
  runtime.queue.pop_front();
  lock.unlock();
  const json message = {{"seq", event.seq}, {"id", event.id}, {"event", event.name},
                        {"payload", json::parse(event.payload)}};
  thread_local std::string text;
  text = message.dump();
  return text.c_str();
}

int pb_host_reply(pb_host* host, long long seq, const char* reply) {
  if (!host) return 1;
  Runtime& runtime = *reinterpret_cast<Runtime*>(host);
  std::shared_ptr<Pending> slot;
  {
    std::lock_guard lock(runtime.mutex);
    const auto found = runtime.pending.find(seq);
    if (found == runtime.pending.end()) return 1;
    slot = found->second;
    runtime.pending.erase(found);
  }
  {
    std::lock_guard lock(slot->mutex);
    slot->reply = reply ? reply : "{}";
    slot->done = true;
  }
  slot->ready.notify_one();
  return 0;
}

void pb_host_stop(pb_host* host) {
  if (!host) return;
  Runtime& runtime = *reinterpret_cast<Runtime*>(host);
  runtime.finish();
  if (runtime.server) runtime.server->stop();
  if (runtime.runner.joinable()) runtime.runner.join();
}

void pb_host_free(pb_host* host) {
  if (!host) return;
  Runtime& runtime = *reinterpret_cast<Runtime*>(host);
  pb_host_stop(host);
  delete &runtime;
}

const char* pb_host_last_error(void) { return g_last_error.c_str(); }

}  // extern "C"
