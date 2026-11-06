#pragma once

// The runtime face of a toolkit block: its definition (what the UI shows) and an
// instance (what runs in a graph). No domain code lives in the toolkit.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace paperboat::toolkit {

// A vector component's optional label and unit. Components are ordered; labels are for
// people, and units are checked by position.
struct VectorComponent {
  std::string id = {};
  std::string unit = {};
};

// A port. Its type: "signal" carries a value; "step" carries control in a Step flow (the
// run passes along a step wire from one block to the next). Others ("points", …) carry
// values of that shape. A wire joins an output to an input of the same `type` ("signal", "image",
// "numbers"…), and step ports only to step ports; "any" takes (or gives) any value but a
// step. With a `unit` on both ends ("N", "m", "rad"…) they must agree; a port without one
// connects to anything of its type. A vector port may declare a fixed `dimension`, and
// optionally ordered `components` with labels and per-component units.
struct PortDef {
  std::string id = {};
  std::string type = "signal";
  std::string unit = {};
  std::optional<std::size_t> dimension = {};
  std::vector<VectorComponent> components = {};
};

// Why a wire may not join output `from` to input `to`, if it may not.
std::optional<std::string> wire_mismatch(const PortDef& from, const PortDef& to);
bool is_vector_type(std::string_view type);
std::optional<std::size_t> vector_dimension(const PortDef& port);

// A typed vector port. With no dimension it accepts any length; component metadata also
// fixes the length. A single `unit` applies to every component not given its own unit.
PortDef vector_port(std::string id);
PortDef vector_port(std::string id, std::size_t dimension);
PortDef vector_port(std::string id, std::vector<VectorComponent> components);

// A setting on a block or a station object, edited in the UI. Number types reach a block
// as numbers (default_value): "number", and "boolean" (1 or 0, a checkbox). Text types
// reach it as text (default_text, through Config): "text", "key" (a KeyboardEvent.code,
// which key a Key block reads), "file" (a path, relative to the descriptor's folder) and
// "enum" (one of `options`, a dropdown; a block also gets its index in `options` as a
// number, so an expression can use it).
struct ParamDef {
  std::string id = {};
  double default_value = 0.0;
  std::string type = "number";
  std::string default_text = {};
  std::vector<std::string> options = {};  // an enum's choices
  std::string unit = {};                  // a number's unit ("N", "m", "rad"…), shown in the UI; "" none

  bool is_number() const { return type == "number" || type == "boolean"; }
  // An enum's choice by name, or -1.
  int option(std::string_view name) const {
    for (std::size_t i = 0; i < options.size(); ++i)
      if (options[i] == name) return static_cast<int>(i);
    return -1;
  }
};

// One form of a magic block: the ports it has in that form, such as "signal" (values in
// and out) and "step" (a step on a Step flow's path). A block's `form` param picks one.
struct Form {
  std::string name = {};
  std::vector<PortDef> inputs = {};
  std::vector<PortDef> outputs = {};
};

// Matches the protocol's BlockDef.kind.
enum class Kind { Source, Probe, Function, Monitor };

std::string_view to_string(Kind kind);

// How a block is drawn: a hint, so a UI without it draws the normal card. Each key is
// optional (unset = not said): a definition inherits its parent's view and overrides single
// keys, as it does params and config.
struct View {
  // Draw the block as a small plate: a glyph, bare port dots (names on hover), the name and
  // main value in a caption. Unset: the normal card.
  std::optional<bool> compact = {};
  // The plate's glyph: a curve name ("gain", "plus", "step", "clamp", "const") or a few
  // characters drawn as text ("&", "≥1", "¬", ">").
  std::string glyph = {};
  // The caption's value, an expression of the params: "{k}", "{min}…{max}".
  std::string value = {};

  bool empty() const { return !compact && glyph.empty() && value.empty(); }
  // This view with `over`'s keys on top.
  View overridden_by(const View& over) const {
    View out = *this;
    if (over.compact) out.compact = over.compact;
    if (!over.glyph.empty()) out.glyph = over.glyph;
    if (!over.value.empty()) out.value = over.value;
    return out;
  }
};

struct BlockDef {
  std::string type = {};  // unique within a catalog, e.g. "pid" or "pid-theta"
  std::string label = {};
  Kind kind = Kind::Function;
  std::vector<PortDef> inputs = {};
  std::vector<PortDef> outputs = {};
  std::vector<ParamDef> params = {};
  // A monitor's instrument on the Dashboard: "graph", "display", "image"…
  std::string instrument = {};
  // A magic block's forms (its inputs and outputs are the first's); its `form` param picks one.
  std::vector<Form> forms = {};
  // The id of a text param that decides the inputs: one per sign character in it ("+-+": a, b,
  // c), see parse_signs. Empty: the inputs are the definition's. Sum's `signs`.
  std::string ports_from = {};
  // False for a block whose outputs this step don't depend on its inputs this step (an
  // integrator, a delay): its Block gives outputs() during the pass and reads its inputs
  // after it, so a feedback loop through it is allowed (no algebraic loop).
  bool feedthrough = true;
  // One line in plain words: what the block does ("Drives an error to zero"). Shown on its
  // card in the browser. Optional.
  std::string description = {};
  // How the UI draws it (a hint; see View). A TOML definition may set `view` and keeps its
  // parent's other keys; package defaults live with the block definition.
  View view = {};
};

// The definition in form `form` (that form's ports); `def` itself if it has no forms, or
// no form of that name.
BlockDef in_form(const BlockDef& def, std::string_view form);

// The signs in `text`, one per input: '+' is +1, '-' (or '−') is −1; anything else (spaces)
// is skipped. At most 26 (inputs a–z); none at all reads as "++".
std::vector<int> parse_signs(std::string_view text);

// Scans `text` for {placeholder} tokens, skipping escaped {{ and }}, returning unique names.
std::vector<std::string> parse_placeholders(std::string_view text);

// The definition with the inputs its `ports_from` param (`text`) gives: ids a, b, c… of the
// first input's type. `def` itself if it has no `ports_from`.
BlockDef with_signs(const BlockDef& def, std::string_view text);

// True for a block on a Step flow's path: one with a step port.
bool has_step_ports(const BlockDef& def);

// A station object type: a thing on the Station page (a body, a joint) whose properties
// are edited only there. A toolkit package can offer these as well as blocks. An object
// may spawn a **linked block**: a block on the Flow page tied to it (same id), through
// which a flow pushes on or reads the object. ISA-88 split: the station is the physical
// model (equipment and its parameters); the flow is the procedure that acts through the
// equipment's linked blocks and never sets its parameters.
struct ObjectDef {
  std::string type = {};                // unique among a catalog's objects, e.g. "hinge"
  std::string label = {};
  std::string role = "body";            // "body" or "joint"
  std::vector<ParamDef> props = {};     // edited on the Station page only
  std::vector<std::string> links = {};  // slots that name other objects, e.g. {"parent", "child"}
  std::optional<BlockDef> block = {};   // the linked block it spawns (its type names it), if any
  std::string description = {};         // one line: what it is; shown on its card. Optional
};

// Data the core passes along without knowing what it is: an image, a mesh, a set of
// records. Packages agree on `kind` (the opencv package uses "image"), which matches the
// ports' type. Shared and immutable, so one output can feed many inputs.
struct Handle {
  std::string kind;
  std::shared_ptr<const void> data;
};

template <class T>
Handle make_handle(std::string kind, T value) {
  return {std::move(kind), std::make_shared<const T>(std::move(value))};
}

// What a wire carries. Most blocks pass numbers; others pass a list of numbers, a table
// (a list of rows, such as points as [x, z]), text, or a handle. The port's type says which.
using Vector = std::vector<double>;
using List = Vector;  // source-compatible name for existing numeric-list values
using Table = std::vector<std::vector<double>>;
struct Value : std::variant<double, Vector, Table, std::string, Handle> {
  using variant::variant;
  // Any number is a double, so {"k", 5} works as well as {"k", 5.0}.
  template <class T>
    requires std::is_arithmetic_v<T> && (!std::is_same_v<T, double>) && (!std::is_same_v<T, bool>)
  Value(T number) : variant(static_cast<double>(number)) {}
  Value(const char* text) : variant(std::string(text)) {}

  friend bool operator==(const Value& v, double number) {
    const double* own = std::get_if<double>(&v);
    return own && *own == number;
  }
};
using Values = std::map<std::string, Value, std::less<>>;        // port or param id → value
using Config = std::map<std::string, std::string, std::less<>>;  // set once in a descriptor
using KeySet = std::set<std::string, std::less<>>;               // KeyboardEvent.code values

// Generalizes dynamic inputs: supports count (inputs = N), template ("{a} + {b}"), and signs ("+-+").
BlockDef with_dynamic_ports(const BlockDef& def, const Values& params, const Config& text);

// Why a value doesn't fit a declared vector port, if it doesn't. Used on block inputs and
// outputs so a dynamic vector is still checked against a fixed-size port at run time.
std::optional<std::string> vector_value_mismatch(const PortDef& port, const Value& value);

class DomainState;
struct Entity;

// Where Context::log writes: the run sends it to the Console as a `log` event.
using LogFn = std::function<void(std::string_view node, std::string_view level, std::string_view text)>;
// Where Context::progress writes: the run sends it as a `node.progress` event.
using ProgressFn = std::function<void(std::string_view node, double fraction)>;

// What a block sees on each step.
struct Context {
  double t = 0.0;   // run clock, s (0 in a run that evaluates once)
  double dt = 0.0;  // step size, s
  const KeySet* keys_down = nullptr;  // from the UI's `input` messages; null when none
  DomainState* state = nullptr;       // the domain's shared state for this run, if it has one
  std::string_view node = {};         // the id of the block being stepped ("" outside a block)
  const LogFn* log_fn = nullptr;      // set by the run; null in tests that don't collect logs
  const ProgressFn* progress_fn = nullptr;  // set by the run; null in tests

  // Write a line to the Console, tagged with the block's id: level "info", "warn" or "error".
  void log(std::string_view text, std::string_view level = "info") const {
    if (log_fn) (*log_fn)(node, level, text);
  }
  // How far a long step has got, 0 to 1 (a filter over a big image, a solve): the UI shows
  // it on the block. Optional; call it as often as is useful, the run sends at most ~20 a second.
  void progress(double fraction) const {
    if (progress_fn) (*progress_fn)(node, fraction);
  }
};

// One instance of a block in a running graph. It owns its state (an integral, the
// previous error), so two PIDs in one graph never share anything.
class Block {
 public:
  virtual ~Block() = default;
  // Compute outputs from inputs. An unconnected input is absent from `in`. For a block
  // without feedthrough (BlockDef::feedthrough false), called after the pass to take this
  // step's inputs; what it returns is ignored, outputs() is what others read.
  virtual Values step(const Context& ctx, const Values& in) = 0;
  // A block without feedthrough: what it outputs now, from its state alone (read during
  // the pass, before its inputs are known). Unused for other blocks.
  virtual Values outputs() const { return {}; }
  // What it draws on the Station after a step (a result to show); most blocks draw nothing.
  virtual std::vector<Entity> entities() const;
};

// One instance of a block on a Step flow's path (a block with step ports). A path is at
// one block at a time: when it arrives, the engine calls enter(), then tick() once per
// clock step until tick() names the step output to leave by. Signal inputs (a condition)
// arrive in `in`, sampled once per clock step.
//
// A block with step outputs and no step input is a **hat**: it starts paths. The engine
// ticks an idle hat every clock step (no enter()), and when it names an output a new path
// starts there, alongside any others. Start fires once; When fires each time `cond` rises.
class Step {
 public:
  virtual ~Step() = default;
  // Its signal outputs (BlockDef outputs of a type other than "step"), such as how far it
  // is through its work. Read each time it is ticked: blocks further on the path see them
  // at once, signal blocks in the next pass, so a wire from a step's output back into it
  // is not a cycle.
  virtual Values outputs() const { return {}; }
  virtual void enter(const Context& /*ctx*/, const Values& /*in*/) {}
  // The step output to leave by, or "" to stay another clock step (a wait, a move).
  virtual std::string tick(const Context& ctx, const Values& in) = 0;
  // For a hat: whether it may still start a path. A run ends when no path is running
  // and no hat may start one.
  virtual bool may_start() const { return false; }
  // True for a block that ends the whole run when a path arrives (Stop).
  virtual bool ends_run() const { return false; }
  // For a hat: true to start its path over when it fires while that path still runs;
  // false lets the running path go on.
  virtual bool restarts() const { return false; }
  // True for a block several paths may be in at once (Join); a path reaching any other
  // block that a path is in waits.
  virtual bool shared() const { return false; }
};

// The number at `id`, or `fallback` when absent or not a number (an unconnected input
// reads as 0).
inline double get_or(const Values& values, std::string_view id, double fallback = 0.0) {
  auto it = values.find(id);
  if (it == values.end()) return fallback;
  const double* number = std::get_if<double>(&it->second);
  return number ? *number : fallback;
}

// The list, table or text at `id`, or null when absent or of another kind.
template <class T>
const T* get_if(const Values& values, std::string_view id) {
  auto it = values.find(id);
  return it == values.end() ? nullptr : std::get_if<T>(&it->second);
}

inline const Vector* get_vector(const Values& values, std::string_view id) { return get_if<Vector>(values, id); }

// The object behind a handle of `kind` at `id`, or null.
template <class T>
const T* get_handle(const Values& values, std::string_view id, std::string_view kind) {
  const Handle* h = get_if<Handle>(values, id);
  return h && h->kind == kind ? static_cast<const T*>(h->data.get()) : nullptr;
}

// Text at `id` (a text param, such as a file name), or `fallback`.
inline std::string get_text(const Values& values, std::string_view id, std::string fallback = {}) {
  const std::string* text = get_if<std::string>(values, id);
  return text ? *text : fallback;
}

// Clean textual representation of any Value (used by to_text, string formatting, etc.).
std::string value_to_text(const Value& value);

}  // namespace paperboat::toolkit
