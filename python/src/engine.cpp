// paperboat._engine: the C++ engine for Python backends.
//
// Python never implements a framework feature: the engine loads the descriptor, checks
// and runs graphs (Signal flow, Step flow, run-once), runs the toolkit's blocks and serves
// the protocol. Python only provides a domain's own code: classes whose attributes define
// them (type, label, ports, params…; read here into BlockDef and ObjectDef, registered as
// the domain's package) and whose methods the engine calls through the adapters below: a
// Python block is a toolkit::Block, a step a toolkit::Step, a station object a
// toolkit::StationObject, a domain state a toolkit::DomainState.
//
// Threads: the engine runs on its own threads; every call into Python takes the GIL, and
// Python errors become C++ errors (which fail the run, never the backend).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>
#include <paperboat/core/backend.hpp>
#include <paperboat/core/backend_manager.hpp>
#include <paperboat/toolkit/bases.hpp>
#include <paperboat/toolkit/model.hpp>
#include <paperboat/toolkit/scene.hpp>
#include <paperboat/toolkit/solve.hpp>

namespace nb = nanobind;
namespace tk = paperboat::toolkit;
namespace core = paperboat::core;
using core::json;

namespace {

// --- Values between C++ and Python ------------------------------------------------------

// Opaque data on a wire (an image…): Python passes it along without looking inside.
struct WireHandle {
  tk::Handle handle;
};

nb::object to_py(const tk::Value& value) {
  if (const auto* d = std::get_if<double>(&value)) return nb::float_(*d);
  if (const auto* s = std::get_if<std::string>(&value)) return nb::str(s->c_str());
  if (const auto* list = std::get_if<tk::List>(&value)) {
    nb::list out;
    for (double v : *list) out.append(nb::float_(v));
    return std::move(out);
  }
  if (const auto* table = std::get_if<tk::Table>(&value)) {
    nb::list out;
    for (const auto& row : *table) {
      nb::list r;
      for (double v : row) r.append(nb::float_(v));
      out.append(r);
    }
    return std::move(out);
  }
  return nb::cast(WireHandle{std::get<tk::Handle>(value)});
}

nb::dict to_py(const tk::Values& values) {
  nb::dict out;
  for (const auto& [k, v] : values) out[k.c_str()] = to_py(v);
  return out;
}

double number(nb::handle h) { return nb::cast<double>(h); }

bool is_number(nb::handle h) { return nb::isinstance<nb::float_>(h) || nb::isinstance<nb::int_>(h); }

tk::Value value_from_py(nb::handle h) {
  if (nb::isinstance<nb::bool_>(h)) return nb::cast<bool>(h) ? 1.0 : 0.0;
  if (is_number(h)) return number(h);
  if (nb::isinstance<nb::str>(h)) return nb::cast<std::string>(h);
  if (nb::isinstance<WireHandle>(h)) return nb::cast<WireHandle>(h).handle;
  if (nb::isinstance<nb::list>(h) || nb::isinstance<nb::tuple>(h)) {
    // A list of numbers, or a table: a list of rows (points as [x, z]).
    bool rows = false;
    for (nb::handle item : h) rows = rows || !is_number(item);
    if (!rows) {
      tk::List list;
      for (nb::handle item : h) list.push_back(number(item));
      return list;
    }
    tk::Table table;
    for (nb::handle row : h) {
      std::vector<double> r;
      for (nb::handle v : row) r.push_back(number(v));
      table.push_back(std::move(r));
    }
    return table;
  }
  throw std::runtime_error("a block returned a value Paperboat can't carry: " + nb::cast<std::string>(nb::repr(h)));
}

tk::Values values_from_py(nb::handle h) {
  tk::Values out;
  if (h.is_none()) return out;
  for (auto [k, v] : nb::cast<nb::dict>(h)) out[nb::cast<std::string>(k)] = value_from_py(v);
  return out;
}

json json_from_py(nb::handle h) {
  if (h.is_none()) return nullptr;
  if (nb::isinstance<nb::bool_>(h)) return nb::cast<bool>(h);
  if (nb::isinstance<nb::int_>(h)) return nb::cast<long long>(h);
  if (nb::isinstance<nb::float_>(h)) return nb::cast<double>(h);
  if (nb::isinstance<nb::str>(h)) return nb::cast<std::string>(h);
  if (nb::isinstance<nb::dict>(h)) {
    json out = json::object();
    for (auto [k, v] : nb::cast<nb::dict>(h)) out[nb::cast<std::string>(k)] = json_from_py(v);
    return out;
  }
  json out = json::array();
  for (nb::handle item : h) out.push_back(json_from_py(item));
  return out;
}

std::array<double, 3> vec3(nb::handle h, std::array<double, 3> fallback) {
  if (h.is_none()) return fallback;
  std::array<double, 3> out = fallback;
  std::size_t i = 0;
  for (nb::handle v : h)
    if (i < 3) out[i++] = number(v);
  return out;
}

tk::Entity entity_from_py(nb::handle h) {
  if (nb::isinstance<tk::Entity>(h)) return nb::cast<tk::Entity>(h);  // from paperboat.scene
  if (!nb::isinstance<nb::dict>(h))
    throw std::runtime_error("an entity is a paperboat.scene shape (scene.rect(…)) or a dict, not " +
                             nb::cast<std::string>(nb::repr(h)));
  const auto d = nb::cast<nb::dict>(h);
  auto get = [&](const char* key) -> nb::object {
    if (!d.contains(key)) return nb::none();
    nb::object v = d[key];
    return v;
  };
  auto text = [&](const char* key, const char* fallback) {
    auto v = get(key);
    return v.is_none() ? std::string(fallback) : nb::cast<std::string>(v);
  };
  return {.id = text("id", ""),
          .shape = text("shape", "box"),
          .pos = vec3(get("pos"), {0, 0, 0}),
          .size = vec3(get("size"), {1, 1, 1}),
          .rot = vec3(get("rot"), {0, 0, 0}),
          .color = text("color", "#888888"),
          .scale = vec3(get("scale"), {1, 1, 1}),
          .to = vec3(get("to"), {0, 0, 0}),
          .src = text("src", "")};
}

// A list of entities; lists in it are flattened, so scene.spring(…) (a list) goes in as is.
void entities_into(nb::handle h, std::vector<tk::Entity>& out) {
  for (nb::handle e : h) {
    if (nb::isinstance<nb::list>(e) || nb::isinstance<nb::tuple>(e)) entities_into(e, out);
    else out.push_back(entity_from_py(e));
  }
}

std::vector<tk::Entity> entities_from_py(nb::handle h) {
  std::vector<tk::Entity> out;
  if (!h.is_none()) entities_into(h, out);
  return out;
}

// A state from Python: any sequence of numbers (a list, a tuple, a numpy array).
tk::solve::State state_from_py(nb::handle h, const char* what) {
  tk::solve::State out;
  try {
    for (nb::handle v : h) out.push_back(nb::cast<double>(v));
  } catch (const std::exception&) {
    throw std::runtime_error(std::string(what) + " must be a list of numbers, not " + nb::cast<std::string>(nb::repr(h)));
  }
  return out;
}

nb::list state_to_py(const tk::solve::State& x) {
  nb::list out;
  for (double v : x) out.append(nb::float_(v));
  return out;
}

// --- What a Python block sees ------------------------------------------------------------

struct BlockContext {
  double t = 0, dt = 0;
  nb::object keys;   // set of KeyboardEvent.code values held down
  nb::object state;  // the domain state object, or None
  std::string node;  // the block's id
  const tk::LogFn* log_fn = nullptr;  // the run's; valid while the run lasts
  const tk::ProgressFn* progress_fn = nullptr;
  const tk::Values* variables = nullptr;
  const tk::SetVariableFn* set_variable_fn = nullptr;

  nb::object variable(const std::string& name) const {
    const tk::Value* v = variables ? [&] { const auto it = variables->find(name); return it == variables->end() ? nullptr : &it->second; }() : nullptr;
    return v ? to_py(*v) : nb::none();
  }
  bool set_variable(const std::string& name, nb::handle value) const {
    return set_variable_fn && (*set_variable_fn)(name, value_from_py(value));
  }
  void log(const std::string& text, const std::string& level) const {
    if (log_fn) (*log_fn)(node, level, text);
  }
  void progress(double fraction) const {
    if (progress_fn) (*progress_fn)(node, fraction);
  }
};

struct StateAdapter;
nb::object state_object(const tk::Context& ctx);

nb::object context(const tk::Context& ctx) {
  nb::set keys;
  if (ctx.keys_down)
    for (const auto& k : *ctx.keys_down) keys.add(nb::str(k.c_str()));
  return nb::cast(BlockContext{ctx.t, ctx.dt, std::move(keys), state_object(ctx), std::string(ctx.node), ctx.log_fn, ctx.progress_fn, ctx.variables, ctx.set_variable_fn});
}

// Runs `fn` with the GIL held; a Python error becomes a C++ error the run reports.
template <class Fn>
auto with_python(Fn&& fn) {
  nb::gil_scoped_acquire gil;
  try {
    return fn();
  } catch (nb::python_error& err) {
    throw std::runtime_error(err.what());
  }
}

// An nb::object that takes the GIL to let go of its reference (engine threads drop these).
class Owned {
 public:
  explicit Owned(nb::object obj) : obj_(std::move(obj)) {}
  ~Owned() {
    nb::gil_scoped_acquire gil;
    obj_ = nb::object();
  }
  Owned(const Owned&) = delete;
  Owned& operator=(const Owned&) = delete;
  const nb::object& operator*() const { return obj_; }

 private:
  nb::object obj_;
};

// --- Adapters: Python domain code as engine interfaces ----------------------------------

struct StateAdapter final : tk::DomainState {
  explicit StateAdapter(nb::object obj) : py(std::move(obj)) {}
  std::vector<tk::Entity> entities() const override {
    return with_python([&] { return entities_from_py((*py).attr("entities")()); });
  }
  std::vector<tk::Entity> patch() const override {
    return with_python([&] { return entities_from_py((*py).attr("patch")()); });
  }
  std::optional<std::string> step(const tk::Context& ctx) override {
    return with_python([&]() -> std::optional<std::string> {
      nb::object r = (*py).attr("step")(context(ctx));
      if (r.is_none()) return std::nullopt;
      return nb::cast<std::string>(r);
    });
  }
  std::optional<std::string> summary() const override {
    return with_python([&]() -> std::optional<std::string> {
      nb::object r = (*py).attr("summary")();
      if (r.is_none()) return std::nullopt;
      return nb::cast<std::string>(r);
    });
  }
  Owned py;
};

nb::object state_object(const tk::Context& ctx) {
  if (auto* s = dynamic_cast<StateAdapter*>(ctx.state)) return *s->py;
  return nb::none();
}

struct BlockAdapter final : tk::Block {
  explicit BlockAdapter(nb::object obj) : py(std::move(obj)) {}
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    return with_python([&] { return values_from_py((*py).attr("step")(context(ctx), to_py(in))); });
  }
  tk::Values outputs() const override { return with_python([&] { return values_from_py((*py).attr("current")()); }); }
  Owned py;
};

struct StepAdapter final : tk::Step {
  explicit StepAdapter(nb::object obj) : py(std::move(obj)) {}
  void enter(const tk::Context& ctx, const tk::Values& in) override {
    with_python([&] { (*py).attr("enter")(context(ctx), to_py(in)); });
  }
  std::string tick(const tk::Context& ctx, const tk::Values& in) override {
    return with_python([&] {
      nb::object r = (*py).attr("tick")(context(ctx), to_py(in));
      return r.is_none() ? std::string() : nb::cast<std::string>(r);
    });
  }
  bool may_start() const override { return with_python([&] { return nb::cast<bool>((*py).attr("may_start")()); }); }
  bool ends_run() const override { return with_python([&] { return nb::cast<bool>((*py).attr("ends_run")()); }); }
  tk::Values outputs() const override { return with_python([&] { return values_from_py((*py).attr("current")()); }); }
  Owned py;
};

// paperboat.Generator: the engine's Generator (f(t), t from the input or the clock) calling
// the Python class's value(t).
struct GeneratorAdapter final : tk::Generator {
  GeneratorAdapter(nb::object obj, std::string output) : tk::Generator(std::move(output)), py(std::move(obj)) {}
  double value(double t) const override { return with_python([&] { return nb::cast<double>((*py).attr("value")(t)); }); }
  Owned py;
};

// paperboat.Timed: the engine's Timed (seconds, curve, progress) calling begin() and
// advance(from, to).
struct TimedAdapter final : tk::Timed {
  TimedAdapter(nb::object obj, double seconds) : tk::Timed(seconds), py(std::move(obj)) {}
  void begin(const tk::Context& ctx, const tk::Values& in) override {
    with_python([&] { (*py).attr("begin")(context(ctx), to_py(in)); });
  }
  void advance(const tk::Context& ctx, double from, double to) override {
    with_python([&] { (*py).attr("advance")(context(ctx), from, to); });
  }
  Owned py;
};

struct ObjectAdapter final : tk::StationObject {
  explicit ObjectAdapter(nb::object obj) : py(std::move(obj)) {}
  tk::Values read() const override { return with_python([&] { return values_from_py((*py).attr("read")()); }); }
  void step(const tk::Context& ctx, const tk::Values& in) override {
    with_python([&] { (*py).attr("step")(context(ctx), to_py(in)); });
  }
  std::vector<tk::Entity> entities(const std::string& id, double z) const override {
    return with_python([&] { return entities_from_py((*py).attr("entities")(nb::str(id.c_str()), z)); });
  }
  void link(std::string_view slot, tk::StationObject& other) override {
    // A Python object gets the other Python object; a toolkit (C++) one can't be reached from Python.
    auto* py_other = dynamic_cast<ObjectAdapter*>(&other);
    with_python([&] {
      (*py).attr("link")(nb::str(slot.data(), slot.size()), py_other ? *py_other->py : nb::none());
    });
  }
  Owned py;
};

// paperboat.Model: the engine's Model (a solver advances the state) calling the Python
// class's derivatives(t, x, inputs). Its `x` is kept current for read() and entities().
struct ModelAdapter final : tk::Model {
  ModelAdapter(nb::object obj, tk::solve::State initial, const std::string& solver)
      : tk::Model(std::move(initial), solver), py(std::move(obj)) {}
  tk::solve::State derivatives(double t, const tk::solve::State& x, const tk::Values&) const override {
    // Called with the GIL held, from step(); `inputs_` is this step's inputs as a dict.
    nb::object dx = (*py).attr("derivatives")(t, state_to_py(x), inputs_);
    return state_from_py(dx, "derivatives()");
  }
  void step(const tk::Context& ctx, const tk::Values& in) override {
    with_python([&] {
      inputs_ = to_py(in);
      tk::Model::step(ctx, in);
      inputs_ = nb::object();
      (*py).attr("x") = state_to_py(x());
    });
  }
  tk::Values read() const override { return with_python([&] { return values_from_py((*py).attr("read")()); }); }
  std::vector<tk::Entity> entities(const std::string& id, double z) const override {
    return with_python([&] { return entities_from_py((*py).attr("entities")(nb::str(id.c_str()), z)); });
  }
  Owned py;
  nb::object inputs_;
};

// --- Definitions: a Python class's attributes as a BlockDef or ObjectDef -----------------

std::string class_name(nb::handle cls) { return nb::cast<std::string>(cls.attr("__name__")); }

std::string text_attr(nb::handle obj, const char* name, const std::string& fallback = {}) {
  if (!nb::hasattr(obj, name)) return fallback;
  nb::object v = obj.attr(name);
  if (v.is_none()) return fallback;
  const auto out = nb::cast<std::string>(v);
  return out.empty() ? fallback : out;
}

tk::VectorComponent vector_component_from_py(nb::handle value, const std::string& at) {
  if (nb::isinstance<nb::str>(value)) return {.id = nb::cast<std::string>(value)};
  if (nb::hasattr(value, "id")) {
    tk::VectorComponent out{.id = nb::cast<std::string>(value.attr("id"))};
    if (nb::hasattr(value, "unit")) out.unit = text_attr(value, "unit");
    return out;
  }
  if (nb::isinstance<nb::tuple>(value) || nb::isinstance<nb::list>(value)) {
    const auto pair = nb::cast<nb::sequence>(value);
    const auto n = nb::len(pair);
    if (n == 1 || n == 2)
      return {.id = nb::cast<std::string>(pair[0]), .unit = n == 2 ? nb::cast<std::string>(pair[1]) : ""};
  }
  throw std::runtime_error(at + ": a vector component is a name, (name, unit) or VectorComponent");
}

// A port: "force", ("x", "position") or paperboat.Port("x", "position").
std::vector<tk::PortDef> ports_from_py(nb::handle list, const std::string& at) {
  std::vector<tk::PortDef> out;
  if (list.is_none()) return out;
  for (nb::handle p : list) {
    if (nb::isinstance<nb::str>(p)) {
      out.push_back({.id = nb::cast<std::string>(p)});
    } else if (nb::hasattr(p, "id") && nb::hasattr(p, "type")) {
      tk::PortDef port{.id = nb::cast<std::string>(p.attr("id")),
                       .type = nb::cast<std::string>(p.attr("type"))};
      if (nb::hasattr(p, "unit")) port.unit = text_attr(p, "unit");
      if (nb::hasattr(p, "dimension") && !p.attr("dimension").is_none()) {
        const int dimension = nb::cast<int>(p.attr("dimension"));
        if (dimension < 1) throw std::runtime_error(at + ": a vector dimension must be positive");
        port.dimension = static_cast<std::size_t>(dimension);
      }
      if (nb::hasattr(p, "components") && !p.attr("components").is_none())
        for (nb::handle component : p.attr("components"))
          port.components.push_back(vector_component_from_py(component, at));
      if (!port.components.empty()) {
        if (!tk::is_vector_type(port.type)) throw std::runtime_error(at + ": components are only valid on a vector port");
        if (port.dimension && *port.dimension != port.components.size())
          throw std::runtime_error(at + ": vector dimension does not match its components");
        port.dimension = port.components.size();
      }
      if (port.dimension && !tk::is_vector_type(port.type))
        throw std::runtime_error(at + ": dimension is only valid on a vector port");
      out.push_back(std::move(port));
    } else if (nb::isinstance<nb::tuple>(p) || nb::isinstance<nb::list>(p)) {
      const auto seq = nb::cast<nb::sequence>(p);
      const auto n = nb::len(seq);
      if (n < 1 || n > 3) throw std::runtime_error(at + ": a port is a name or (name, type, unit)");
      out.push_back({.id = nb::cast<std::string>(seq[0]),
                     .type = n > 1 ? nb::cast<std::string>(seq[1]) : "signal",
                     .unit = n > 2 ? nb::cast<std::string>(seq[2]) : ""});
    } else {
      throw std::runtime_error(at + ": a port is a name or paperboat.Port(name, type), not " +
                               nb::cast<std::string>(nb::repr(p)));
    }
  }
  return out;
}

// Params (or props): {"k": 2.0, "key": paperboat.Param("Space", "key")}, in that order.
std::vector<tk::ParamDef> params_from_py(nb::handle dict, const std::string& at) {
  std::vector<tk::ParamDef> out;
  if (dict.is_none()) return out;
  for (auto [k, v] : nb::cast<nb::dict>(dict)) {
    const auto id = nb::cast<std::string>(k);
    if (nb::isinstance<nb::bool_>(v)) {
      out.push_back({.id = id, .default_value = nb::cast<bool>(v) ? 1.0 : 0.0, .type = "boolean"});
    } else if (is_number(v)) {
      out.push_back({.id = id, .default_value = nb::cast<double>(v)});
    } else if (nb::isinstance<nb::str>(v)) {
      out.push_back({.id = id, .type = "text", .default_text = nb::cast<std::string>(v)});
    } else if (nb::isinstance<nb::tuple>(v) && (nb::len(nb::cast<nb::tuple>(v)) == 2 || nb::len(nb::cast<nb::tuple>(v)) == 3)) {
      const auto t = nb::cast<nb::tuple>(v);  // paperboat.Param(default, type, options)
      tk::ParamDef p{.id = id, .type = nb::cast<std::string>(t[1])};
      if (p.type != "number" && p.type != "boolean" && p.type != "text" && p.type != "key" && p.type != "file" && p.type != "enum" && p.type != "variable")
        throw std::runtime_error(at + ": param " + id + " has type \"" + p.type + "\" (number, boolean, text, key, file, enum or variable)");
      if (p.is_number()) p.default_value = nb::cast<double>(t[0]);
      else p.default_text = nb::cast<std::string>(t[0]);
      if (nb::len(t) == 3)
        for (nb::handle o : t[2]) p.options.push_back(nb::cast<std::string>(o));
      if (p.type == "enum" && p.option(p.default_text) < 0)
        throw std::runtime_error(at + ": enum param " + id + " needs options that include its default");
      out.push_back(p);
    } else {
      throw std::runtime_error(at + ": param " + id + " needs a default: a number, a bool, text or paperboat.Param(default, type)");
    }
  }
  return out;
}

tk::Kind kind_from(const std::string& s, const std::string& at) {
  if (s == "source") return tk::Kind::Source;
  if (s == "probe") return tk::Kind::Probe;
  if (s == "function") return tk::Kind::Function;
  if (s == "monitor") return tk::Kind::Monitor;
  throw std::runtime_error(at + ": kind must be source, probe, function or monitor, not \"" + s + "\"");
}

// A block class's `view` dict: {"compact": True, "glyph": "gain", "value": "{k}", "icon": {...}}, all optional.
tk::View view_from_py(nb::handle cls, const std::string& at) {
  tk::View view;
  if (!nb::hasattr(cls, "view")) return view;
  nb::object v = cls.attr("view");
  if (v.is_none()) return view;
  if (!nb::isinstance<nb::dict>(v)) throw std::runtime_error(at + ": view must be a dict (compact, glyph, value, icon)");
  for (auto [k, val] : nb::cast<nb::dict>(v)) {
    const auto key = nb::cast<std::string>(k);
    if (key == "compact") view.compact = nb::cast<bool>(val);
    else if (key == "glyph") view.glyph = nb::cast<std::string>(val);
    else if (key == "value") view.value = nb::cast<std::string>(val);
    else if (key == "icon") {
      if (!val.is_none()) {
        json icon = json_from_py(val);
        if (!icon.is_object()) throw std::runtime_error(at + ": view.icon must be a dict");
        view.icon = std::move(icon);
      }
    } else throw std::runtime_error(at + ": view has no key \"" + key + "\" (compact, glyph, value, icon)");
  }
  return view;
}

tk::BlockDef block_def(nb::handle cls) {
  const std::string at = class_name(cls);
  tk::BlockDef def;
  def.type = text_attr(cls, "type");
  if (def.type.empty()) throw std::runtime_error(at + ": a block class needs `type` (its id in the catalog)");
  def.label = text_attr(cls, "label", def.type);
  def.kind = kind_from(text_attr(cls, "kind", "function"), at);
  def.inputs = ports_from_py(cls.attr("inputs"), at);
  def.outputs = ports_from_py(cls.attr("outputs"), at);
  def.params = params_from_py(cls.attr("params"), at);
  if (def.kind == tk::Kind::Monitor) def.instrument = text_attr(cls, "instrument", "graph");
  if (nb::hasattr(cls, "feedthrough")) def.feedthrough = nb::cast<bool>(cls.attr("feedthrough"));
  def.description = text_attr(cls, "description", "");
  def.view = view_from_py(cls, at);
  def.ports_from = text_attr(cls, "ports_from", "");
  def.inputs_from = text_attr(cls, "inputs_from", "");
  def.outputs_from = text_attr(cls, "outputs_from", "");
  if (nb::hasattr(cls, "elementwise")) def.elementwise = nb::cast<bool>(cls.attr("elementwise"));
  return def;
}

tk::ObjectDef object_def(nb::handle cls) {
  const std::string at = class_name(cls);
  tk::ObjectDef def;
  def.type = text_attr(cls, "type");
  if (def.type.empty()) throw std::runtime_error(at + ": a station object class needs `type`");
  def.label = text_attr(cls, "label", def.type);
  def.role = text_attr(cls, "role", "body");
  def.props = params_from_py(cls.attr("props"), at);
  for (nb::handle l : cls.attr("links")) def.links.push_back(nb::cast<std::string>(l));
  nb::object linked = cls.attr("block");
  if (!linked.is_none()) {
    def.block = tk::BlockDef{.type = def.type,
                             .label = text_attr(linked, "label", def.label),
                             .kind = kind_from(text_attr(linked, "kind", "probe"), at + ".block"),
                             .inputs = ports_from_py(linked.attr("inputs"), at + ".block"),
                             .outputs = ports_from_py(linked.attr("outputs"), at + ".block"),
                             .view = view_from_py(linked, at + ".block")};
  }
  def.description = text_attr(cls, "description", "");
  return def;
}

nb::dict params_dict(const tk::Values& params, const tk::Config& text) {
  nb::dict d = to_py(params);
  for (const auto& [k, v] : text) d[k.c_str()] = nb::str(v.c_str());
  return d;
}

// --- A backend hosted by Python ----------------------------------------------------------

// Everything a backend points at, kept alive together.
struct Hosted {
  ~Hosted() {
    nb::gil_scoped_release free;  // stopping joins runs that may be waiting for the GIL
    server.reset();
  }
  tk::Registry registry;
  tk::Catalog catalog;
  nb::object classes;  // the domain's block and station object classes, kept alive
  nb::object state;    // a class (or factory) for the domain state, or None
  core::Backend backend;
  std::unique_ptr<core::Server> server;

  void listen(int port) {
    server = std::make_unique<core::Server>(backend, port);
    std::string error;
    {
      nb::gil_scoped_release free;
      error = server->start();
    }
    if (!error.empty()) throw std::runtime_error("cannot listen on port " + std::to_string(port) + ": " + error);
  }

  std::string run_starter(const std::vector<std::pair<std::string, std::string>>& vars,
                          std::optional<double> duration = std::nullopt) {
    nb::gil_scoped_release free;  // the run takes the GIL for each call into Python
    return core::run_starter(backend, std::cout, vars, duration);
  }

  void stop() {
    nb::gil_scoped_release free;  // sessions join runs that may be waiting for the GIL
    if (server) server->stop();
  }
};

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// The domain's classes become its package (named after the descriptor's `domain`), next to
// the built-in toolkit; then the descriptor loads against that registry.
std::unique_ptr<Hosted> make(const std::string& engine, const std::string& descriptor, nb::list blocks, nb::list objects,
                             nb::object state, const std::string& mode, double dt, double duration, int emit_every,
                             nb::object view, double speed, std::string solver, nb::object connectors) {
  auto h = std::make_unique<Hosted>();
  if (solver.empty()) {  // not set by the backend: the backend.toml beside the descriptor, else rk4
    const auto dir = std::filesystem::path(descriptor).parent_path();
    if (std::filesystem::is_regular_file(dir / "backend.toml")) {
      std::string err;
      solver = core::BackendManager::load_manifest(dir, &err).solver;
      if (!err.empty()) throw std::runtime_error(err);
    }
    if (solver.empty()) solver = "rk4";
  }
  h->registry = tk::builtins();
  const std::string domain = tk::descriptor_domain(read_file(descriptor));
  if (domain.empty() && (nb::len(blocks) > 0 || nb::len(objects) > 0))
    throw std::runtime_error(descriptor + ": needs a `domain` to name the package of the domain's own blocks");
  nb::list keep;
  for (nb::handle cls : blocks) {
    keep.append(cls);
    tk::Template t;
    t.def = block_def(cls);
    t.name = domain + "/" + t.def.type;
    const std::string base = text_attr(cls, "_paperboat");
    if (base == "timed") {
      if (std::ranges::find(t.def.params, std::string("seconds"), &tk::ParamDef::id) == t.def.params.end())
        throw std::runtime_error(class_name(cls) + ": a Timed step needs a `seconds` param");
      t.make_step = [cls](const tk::Values& p, const tk::Config& c) -> std::unique_ptr<tk::Step> {
        return with_python([&] { return std::make_unique<TimedAdapter>(cls(params_dict(p, c)), tk::get_or(p, "seconds", 1)); });
      };
    } else if (base == "generator") {
      const std::string output = t.def.outputs.empty() ? "y" : t.def.outputs.front().id;
      t.make = [cls, output](const tk::Values& p, const tk::Config& c) -> std::unique_ptr<tk::Block> {
        return with_python([&] { return std::make_unique<GeneratorAdapter>(cls(params_dict(p, c)), output); });
      };
    } else if (base == "step") {
      t.make_step = [cls](const tk::Values& p, const tk::Config& c) -> std::unique_ptr<tk::Step> {
        return with_python([&] { return std::make_unique<StepAdapter>(cls(params_dict(p, c))); });
      };
    } else {
      t.make = [cls](const tk::Values& p, const tk::Config& c) -> std::unique_ptr<tk::Block> {
        return with_python([&] { return std::make_unique<BlockAdapter>(cls(params_dict(p, c))); });
      };
    }
    h->registry.add(std::move(t));
  }
  for (nb::handle cls : objects) {
    keep.append(cls);
    tk::StationTemplate t;
    t.def = object_def(cls);
    t.name = domain + "/" + t.def.type;
    if (text_attr(cls, "_paperboat") == "model") {
      t.make = [cls, default_solver = solver](const tk::Values& props) -> std::unique_ptr<tk::StationObject> {
        return with_python([&]() -> std::unique_ptr<tk::StationObject> {
          nb::object obj = cls(to_py(props));
          auto initial = state_from_py(obj.attr("initial")(), "initial()");
          // A `solver` prop (an enum the user picks) wins over the class's `solver`.
          const std::string s = tk::get_text(props, "solver", text_attr(cls, "solver", default_solver));
          obj.attr("x") = state_to_py(initial);
          return std::make_unique<ModelAdapter>(obj, std::move(initial), s);
        });
      };
    } else {
      t.make = [cls](const tk::Values& props) -> std::unique_ptr<tk::StationObject> {
        return with_python([&] { return std::make_unique<ObjectAdapter>(cls(to_py(props))); });
      };
    }
    h->registry.add(std::move(t));
  }
  if (!connectors.is_none()) {
    if (!nb::isinstance<nb::dict>(connectors)) throw std::runtime_error("connectors must be a dict");
    if (domain.empty()) throw std::runtime_error(descriptor + ": needs a `domain` when registering connectors");
    for (auto [k, val] : nb::cast<nb::dict>(connectors)) {
      const auto name = nb::cast<std::string>(k);
      json def = json_from_py(val);
      if (!def.is_object()) throw std::runtime_error("connectors." + name + " must be a dict");
      h->registry.add_connector(domain, name, std::move(def));
    }
  }
  h->classes = keep;
  h->catalog = tk::load_descriptor_file(descriptor, h->registry);
  h->state = state;

  core::Backend& b = h->backend;
  b.engine = engine;
  b.catalog = &h->catalog;
  b.view = json_from_py(view);
  b.run = {.mode = mode == "once" ? core::RunSettings::Mode::Once : core::RunSettings::Mode::Clock,
           .dt = dt,
           .duration = duration,
           .emit_every = emit_every,
           .real_time = true,
           .speed = speed,
           .solver = solver};
  if (!state.is_none()) {
    nb::handle factory = h->state;
    b.state = [factory]() -> std::unique_ptr<tk::DomainState> {
      return with_python([&] { return std::make_unique<StateAdapter>(factory()); });
    };
  }
  return h;
}

}  // namespace

NB_MODULE(_engine, m) {
  m.doc() = "Paperboat's C++ engine. Use the `paperboat` package, not this module.";

  nb::class_<WireHandle>(m, "Handle", "Opaque data on a wire (an image…); pass it along as is.")
      .def_prop_ro("kind", [](const WireHandle& h) { return h.handle.kind; });

  nb::class_<BlockContext>(m, "Context", "What a block sees on each step.")
      .def_ro("t", &BlockContext::t, "run clock, s")
      .def_ro("dt", &BlockContext::dt, "step size, s")
      .def_ro("keys", &BlockContext::keys, "keys held down in the UI (KeyboardEvent.code values)")
      .def_ro("state", &BlockContext::state, "the domain state for this run, or None")
      .def_ro("node", &BlockContext::node, "the id of the block (or station object) being called")
      .def("log", &BlockContext::log, nb::arg("text"), nb::arg("level") = "info",
           "Write a line to the Console, tagged with this block's id (level: info, warn, error).")
      .def("variable", &BlockContext::variable, nb::arg("name"),
           "A project variable's value now (a number, a boolean as 1.0 / 0.0, or text), or None.")
      .def("set_variable", &BlockContext::set_variable, nb::arg("name"), nb::arg("value"),
           "Set a tunable variable (a number, or text for enum / text / file ones); it takes effect at the end "
           "of the pass and is kept in the project. False, with a Console warning, if refused.")
      .def("progress", &BlockContext::progress, nb::arg("fraction"),
           "How far a long step has got, 0 to 1: the UI shows it on the block.");

  nb::class_<Hosted>(m, "Hosted", "A backend: the domain's package, its catalog and (once listening) a server.")
      .def("listen", &Hosted::listen, nb::arg("port"), "Serve the protocol (and a web station) on the port.")
      .def("run_starter", &Hosted::run_starter, nb::arg("vars") = std::vector<std::pair<std::string, std::string>>{},
           nb::arg("duration").none() = nb::none(),
           "Run the starter graph on the starter station once, headless; print each message as a JSON line.")
      .def("catalog_json", [](const Hosted& h) { return tk::catalog_message(h.catalog); },
           "Return the protocol catalog JSON message for this backend.")
      .def("stop", &Hosted::stop);

  m.def("make", &make, nb::arg("engine"), nb::arg("descriptor"), nb::arg("blocks"), nb::arg("objects"),
        nb::arg("state").none(), nb::arg("mode"), nb::arg("dt"), nb::arg("duration"), nb::arg("emit_every"),
        nb::arg("view").none(), nb::arg("speed") = 1.0, nb::arg("solver") = "",
        nb::arg("connectors").none() = nb::none(),
        "Register the domain's classes and load the descriptor.");

  // --- Domain code's tools: the scene builders and solvers, bound from the toolkit ---

  nb::class_<tk::Entity>(m, "Entity", "One shape in the scene (the protocol's Entity); made by paperboat.scene.")
      .def(nb::init<>())
      .def_rw("id", &tk::Entity::id)
      .def_rw("shape", &tk::Entity::shape)
      .def_rw("pos", &tk::Entity::pos)
      .def_rw("size", &tk::Entity::size)
      .def_rw("rot", &tk::Entity::rot)
      .def_rw("color", &tk::Entity::color)
      .def_rw("scale", &tk::Entity::scale)
      .def_rw("to", &tk::Entity::to)
      .def_rw("src", &tk::Entity::src)
      .def("__repr__", [](const tk::Entity& e) {
        return "Entity(" + e.shape + " " + e.id + " at " + std::to_string(e.pos[0]) + ", " + std::to_string(e.pos[1]) +
               ", " + std::to_string(e.pos[2]) + ")";
      });

  // Points are (x, y) or (x, y, z): a 2D scene needn't say z.
  auto point = [](nb::handle h) { return vec3(h, {0, 0, 0}); };
  nb::module_ scene = m.def_submodule("scene", "Scene builders: entities by what they are (paperboat.scene.rect…).");
  scene.def("box", [point](std::string id, nb::handle pos, nb::handle size, std::string color, nb::handle rot) {
      return tk::scene::box(std::move(id), point(pos), vec3(size, {1, 1, 1}), std::move(color), point(rot));
    }, nb::arg("id"), nb::arg("pos"), nb::arg("size"), nb::arg("color") = "#888888", nb::arg("rot") = nb::none(),
    "A box centred at pos, turned by rot (radians, about x, then y, then z).");
  scene.def("sphere", [point](std::string id, nb::handle pos, double radius, std::string color) {
      return tk::scene::sphere(std::move(id), point(pos), radius, std::move(color));
    }, nb::arg("id"), nb::arg("pos"), nb::arg("radius"), nb::arg("color") = "#888888", "A ball centred at pos.");
  scene.def("line", [point](std::string id, nb::handle a, nb::handle b, std::string color) {
      return tk::scene::line(std::move(id), point(a), point(b), std::move(color));
    }, nb::arg("id"), nb::arg("a"), nb::arg("b"), nb::arg("color") = "#888888", "A straight line from a to b.");
  scene.def("image", [point](std::string id, nb::handle pos, double width, double height, std::string src) {
      return tk::scene::image(std::move(id), point(pos), width, height, std::move(src));
    }, nb::arg("id"), nb::arg("pos"), nb::arg("width"), nb::arg("height"), nb::arg("src"),
    "A picture width × height facing +z, centred at pos: a URL or a data: URL.");
  scene.def("rect", &tk::scene::rect, nb::arg("id"), nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"),
            nb::arg("color") = "#888888", nb::arg("angle") = 0.0, nb::arg("z") = 0.0,
            "A rectangle in the x-y plane centred at (x, y), turned by angle (radians, anticlockwise).");
  scene.def("circle", &tk::scene::circle, nb::arg("id"), nb::arg("x"), nb::arg("y"), nb::arg("radius"),
            nb::arg("color") = "#888888", nb::arg("z") = 0.0, "A disc in the x-y plane centred at (x, y).");
  scene.def("rod", [point](std::string id, nb::handle a, nb::handle b, double thickness, std::string color) {
      return tk::scene::rod(std::move(id), point(a), point(b), thickness, std::move(color));
    }, nb::arg("id"), nb::arg("a"), nb::arg("b"), nb::arg("thickness"), nb::arg("color") = "#888888",
    "A bar of square section from a to b (a link, a pole, a strut).");
  scene.def("spring", [point](const std::string& id, nb::handle a, nb::handle b, int coils, double width, const std::string& color) {
      return tk::scene::spring(id, point(a), point(b), coils, width, color);
    }, nb::arg("id"), nb::arg("a"), nb::arg("b"), nb::arg("coils") = 8, nb::arg("width") = 0.2, nb::arg("color") = "#888888",
    "A coil spring from a to b: a list of lines (ids id/0, id/1…), the same number at any length.");

  nb::module_ solve = m.def_submodule("solve", "Solvers: advance x' = f(t, x) (or x[k+1] = f(t, x)) by a clock step.");
  struct PySolver {
    std::unique_ptr<tk::solve::Solver> solver;
    nb::list step(nb::callable f, nb::handle x, double t, double dt) {
      auto state = state_from_py(x, "x");
      solver->step(state, t, dt, [&](double at, const tk::solve::State& s) {
        return state_from_py(f(at, state_to_py(s)), "f(t, x)");
      });
      return state_to_py(state);
    }
  };
  nb::class_<PySolver>(solve, "Solver", "A solver by name (paperboat.solve.names()); rk45 keeps its step size between calls, cvode and ida their history.")
      .def("__init__", [](PySolver* self, const std::string& name, double rtol, double atol, int max_steps) {
          new (self) PySolver{tk::solve::make(name, {.rtol = rtol, .atol = atol, .max_steps = max_steps})};
        }, nb::arg("name") = "rk4", nb::arg("rtol") = 1e-6, nb::arg("atol") = 1e-9, nb::arg("max_steps") = 100000)
      .def("step", &PySolver::step, nb::arg("f"), nb::arg("x"), nb::arg("t"), nb::arg("dt"),
           "x advanced from t to t + dt under f(t, x) (dx/dt, or the next x for discrete): a new list.")
      .def("restart", [](PySolver& s) { s.solver->restart(); },
           "Forget what it carries between steps: call it after the equations change.")
      .def_prop_ro("name", [](const PySolver& s) { return std::string(s.solver->name()); });
  solve.def("names", [] { return tk::solve::names(); }, "The solvers, the default first: rk4, rk45, rk2, euler, discrete, then cvode and ida (implicit, for stiff systems) when the engine has SUNDIALS.");
  solve.def("step", [](const std::string& name, nb::callable f, nb::handle x, double t, double dt) {
      PySolver s{tk::solve::make(name)};
      return s.step(std::move(f), x, t, dt);
    }, nb::arg("solver"), nb::arg("f"), nb::arg("x"), nb::arg("t"), nb::arg("dt"),
    "One step with a fresh solver of that name: x advanced from t to t + dt, a new list.");

  m.def(
      "options",
      [](const std::vector<std::string>& argv, int default_port) {
        std::vector<char*> args;
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        const auto o = core::parse_options(static_cast<int>(args.size()), args.data(), default_port);
        return std::make_tuple(o.port, o.run, o.vars, o.duration);
      },
      nb::arg("argv"), nb::arg("default_port"), "(port, run, vars, duration) from a command line: --port N, --run, --duration S, --var name=value.");
  m.def("remember_args", nb::overload_cast<std::vector<std::string>>(&core::remember_args), nb::arg("args"),
        "Remember the process command line for backend.restart.");
}
