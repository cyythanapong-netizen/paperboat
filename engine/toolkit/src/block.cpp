#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/expression.hpp"
#include "paperboat/toolkit/station.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace paperboat::toolkit {

std::string_view to_string(Kind kind) {
  switch (kind) {
    case Kind::Source: return "source";
    case Kind::Probe: return "probe";
    case Kind::Function: return "function";
    case Kind::Monitor: return "monitor";
  }
  return "function";
}

std::vector<Entity> Block::entities() const { return {}; }

std::string_view monitor_state_name(MonitorState state) {
  switch (state) {
    case MonitorState::Ok: return "ok";
    case MonitorState::Warn: return "warn";
    case MonitorState::Trip: return "trip";
  }
  return "ok";
}

BlockDef in_form(const BlockDef& def, std::string_view form) {
  for (const auto& f : def.forms) {
    if (f.name != form) continue;
    BlockDef out = def;
    out.inputs = f.inputs;
    out.outputs = f.outputs;
    return out;
  }
  return def;
}

std::vector<int> parse_signs(std::string_view text) {
  std::vector<int> out;
  for (std::size_t i = 0; i < text.size() && out.size() < 26; ++i) {
    if (text[i] == '+') out.push_back(1);
    else if (text[i] == '-') out.push_back(-1);
    else if (text.substr(i, 3) == "\xE2\x88\x92") {  // U+2212 MINUS SIGN
      out.push_back(-1);
      i += 2;
    }
  }
  return out.empty() ? std::vector<int>{1, 1} : out;
}

std::vector<std::string> parse_placeholders(std::string_view text) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '{') {
      if (i + 1 < text.size() && text[i + 1] == '{') {
        ++i;  // escaped {{
        continue;
      }
      auto end = text.find('}', i + 1);
      if (end == std::string_view::npos) break;
      auto inner = text.find('{', i + 1);
      if (inner != std::string_view::npos && inner < end) {
        i = inner - 1;
        continue;
      }
      std::string_view token = text.substr(i + 1, end - (i + 1));
      while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
      while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
      if (!token.empty()) {
        std::string name(token);
        if (seen.insert(name).second) {
          out.push_back(std::move(name));
        }
      }
      i = end;
    } else if (text[i] == '}') {
      if (i + 1 < text.size() && text[i + 1] == '}') {
        ++i;  // escaped }}
      }
    }
  }
  return out;
}

std::vector<std::string> parse_names(std::string_view text, const std::vector<std::string>& reserved) {
  std::vector<std::string> out;
  std::string name;
  const auto take = [&] {
    if (name.empty()) return;
    const bool ident = !std::isdigit(static_cast<unsigned char>(name[0])) &&
                       std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    if (!ident) throw std::invalid_argument("names: \"" + name + "\" isn't a name (letters, digits and _, not starting with a digit)");
    if (std::ranges::find(out, name) != out.end()) throw std::invalid_argument("names: \"" + name + "\" is there twice");
    if (std::ranges::find(reserved, name) != reserved.end())
      throw std::invalid_argument("names: \"" + name + "\" is the id of the block's other port; pick another");
    out.push_back(std::move(name));
    name.clear();
  };
  for (const char c : text) {
    if (c == ' ' || c == ',' || c == '\t' || c == '\n') take();
    else name += c;
  }
  take();
  if (out.empty()) throw std::invalid_argument("names: give at least one name");
  if (out.size() > 64) throw std::invalid_argument("names: at most 64 names, not " + std::to_string(out.size()));
  return out;
}

namespace {

// A text param's value: given in `text`, else its default.
std::string text_param(const BlockDef& def, const Config& text, const std::string& id) {
  if (auto it = text.find(id); it != text.end()) return it->second;
  auto p = std::ranges::find(def.params, id, &ParamDef::id);
  return p == def.params.end() ? std::string{} : p->default_text;
}

// `names` become ports of one side (`side`), units by position from the `units` param; the
// first vector or points port of the other side (`other`) gets one component per name.
void name_ports(const BlockDef& def, const Config& text, const std::string& param, std::vector<PortDef>& side,
                std::vector<PortDef>& other) {
  std::vector<std::string> reserved;
  for (const auto& p : other) reserved.push_back(p.id);
  const auto names = parse_names(text_param(def, text, param), reserved);
  // Units by position, split like names ("m m/s rad", "m, -, rad"); "-" is none.
  std::string unit_text = text_param(def, text, "units");
  std::ranges::replace(unit_text, ',', ' ');
  std::istringstream split(unit_text);
  std::vector<std::string> units;
  for (std::string u; split >> u;) units.push_back(u);
  const PortDef proto = side.empty() ? PortDef{} : side.front();
  side.clear();
  std::vector<VectorComponent> components;
  for (std::size_t i = 0; i < names.size(); ++i) {
    PortDef p = proto;
    p.id = names[i];
    p.unit = i < units.size() && units[i] != "-" ? units[i] : "";
    components.push_back({.id = names[i], .unit = p.unit});
    side.push_back(std::move(p));
  }
  // (or points port: one coordinate per name, as a Points block's [x, z])
  if (auto v = std::ranges::find_if(other, [](const PortDef& p) { return is_vector_type(p.type) || p.type == "points"; });
      v != other.end()) {
    v->dimension = names.size();
    v->components = std::move(components);
  }
}

BlockDef with_ports_from(const BlockDef& def, const Values& params, const Config& text) {
  BlockDef out = def;

  if (def.ports_from == "template") {
    std::string_view tmpl_text = "Hello {name}";
    if (auto it = text.find("template"); it != text.end()) {
      tmpl_text = it->second;
    } else {
      for (const auto& p : def.params) {
        if (p.id == "template" && !p.default_text.empty()) {
          tmpl_text = p.default_text;
          break;
        }
      }
    }
    const auto names = parse_placeholders(tmpl_text);
    out.inputs.clear();
    for (const auto& name : names) {
      out.inputs.push_back({.id = name, .type = "any"});
    }
    return out;
  }

  if (def.ports_from == "inputs" || params.contains(def.ports_from)) {
    int count = 2;
    if (auto it = params.find(def.ports_from); it != params.end()) {
      if (const double* d = std::get_if<double>(&it->second)) {
        count = static_cast<int>(*d);
      }
    } else {
      for (const auto& p : def.params) {
        if (p.id == def.ports_from && p.is_number()) {
          count = static_cast<int>(p.default_value);
          break;
        }
      }
    }
    count = std::clamp(count, 1, 26);
    const PortDef proto = def.inputs.empty() ? PortDef{.id = "a", .type = "signal"} : def.inputs.front();
    out.inputs.clear();
    for (int i = 0; i < count; ++i) {
      PortDef p = proto;
      p.id = std::string(1, static_cast<char>('a' + i));
      out.inputs.push_back(std::move(p));
    }
    return out;
  }

  std::string_view signs_text = "";
  if (auto it = text.find(def.ports_from); it != text.end()) {
    signs_text = it->second;
  }
  const PortDef proto = def.inputs.empty() ? PortDef{.id = "a", .type = "signal"} : def.inputs.front();
  out.inputs.clear();
  for (std::size_t i = 0; i < parse_signs(signs_text).size(); ++i) {
    PortDef p = proto;
    p.id = std::string(1, static_cast<char>('a' + i));
    out.inputs.push_back(std::move(p));
  }
  return out;
}

}  // namespace

BlockDef with_dynamic_ports(const BlockDef& def, const Values& params, const Config& text) {
  BlockDef out = def;
  if (!def.outputs_from.empty()) name_ports(def, text, def.outputs_from, out.outputs, out.inputs);
  if (def.ports_from == "names") name_ports(def, text, def.ports_from, out.inputs, out.outputs);
  else if (!def.ports_from.empty()) out = with_ports_from(out, params, text);
  return out;
}

BlockDef with_signs(const BlockDef& def, std::string_view text) {
  if (def.ports_from.empty()) return def;
  Config cfg;
  cfg[def.ports_from] = std::string(text);
  return with_dynamic_ports(def, {}, cfg);
}

BlockDef with_inputs(const BlockDef& def, std::string_view text) {
  if (def.inputs_from.empty()) return def;
  BlockDef out = def;
  const PortDef proto = def.inputs.empty() ? PortDef{} : def.inputs.front();
  out.inputs.clear();
  for (const auto& name : Expression::free_names(text)) {
    PortDef p = proto;
    p.id = name;
    out.inputs.push_back(std::move(p));
  }
  return out;
}

std::string value_to_text(const Value& value) {
  struct Visitor {
    std::string operator()(double d) const {
      if (std::isnan(d)) return "nan";
      if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
      if (d == 0.0) return "0";
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.12g", d);
      return buf;
    }
    std::string operator()(const std::string& s) const { return s; }
    std::string operator()(const Vector& vec) const {
      std::string s = "[";
      for (std::size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) s += ", ";
        s += operator()(vec[i]);
      }
      s += "]";
      return s;
    }
    std::string operator()(const Table& tbl) const {
      std::string s = "[";
      for (std::size_t i = 0; i < tbl.size(); ++i) {
        if (i > 0) s += ", ";
        s += operator()(tbl[i]);
      }
      s += "]";
      return s;
    }
    std::string operator()(const Handle& h) const {
      return "<handle:" + (h.kind.empty() ? "generic" : h.kind) + ">";
    }
  };
  return std::visit(Visitor{}, value);
}

bool has_step_ports(const BlockDef& def) {
  auto step = [](const PortDef& p) { return p.type == "step"; };
  return std::ranges::any_of(def.inputs, step) || std::ranges::any_of(def.outputs, step);
}

const std::vector<std::string>& flow_types() {
  static const std::vector<std::string> types{"signal", "step", "state", "model"};
  return types;
}

bool is_flow_type(std::string_view flow) { return std::ranges::find(flow_types(), flow) != flow_types().end(); }

std::string flow_label(std::string_view flow) {
  std::string out(flow);
  if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out + " flow";
}

std::string flows_label(const std::vector<std::string>& flows) {
  if (flows.empty()) return "every flow type";
  std::string out;
  for (std::size_t i = 0; i < flows.size(); ++i)
    out += (i == 0 ? "" : i + 1 == flows.size() ? " or " : ", ") + flow_label(flows[i]);
  return out;
}

void check_flows(const std::vector<std::string>& flows, const std::string& at) {
  if (flows.empty()) throw std::invalid_argument(at + ": flows is empty; leave it out for every flow type");
  std::string known;
  for (const auto& f : flow_types()) known += (known.empty() ? "" : ", ") + f;
  for (std::size_t i = 0; i < flows.size(); ++i) {
    if (!is_flow_type(flows[i])) throw std::invalid_argument(at + ": no flow type \"" + flows[i] + "\" (" + known + ")");
    if (std::ranges::find(flows.begin(), flows.begin() + static_cast<std::ptrdiff_t>(i), flows[i]) != flows.begin() + static_cast<std::ptrdiff_t>(i))
      throw std::invalid_argument(at + ": flow type \"" + flows[i] + "\" is listed twice");
  }
}

bool works_in(const BlockDef& def, std::string_view flow) {
  return def.flows.empty() || std::ranges::find(def.flows, flow) != def.flows.end();
}

namespace {
std::string a_port(const std::string& type) {
  return std::string(std::string_view("aeiou").find(type.empty() ? 'x' : type[0]) != std::string_view::npos ? "an " : "a ") + type + " port";
}
}  // namespace

namespace {
// Why `from`'s shape doesn't fit `to`'s, for two vector ports or two points ports: the declared
// length (a vector's values, a point's coordinates) and the units by position.
std::optional<std::string> shape_mismatch(const PortDef& from, const PortDef& to, bool points) {
  const auto from_size = vector_dimension(from);
  const auto to_size = vector_dimension(to);
  if (from_size && to_size && *from_size != *to_size)
    return points ? "carries points of " + std::to_string(*from_size) + " coordinates into a port expecting " +
                        std::to_string(*to_size)
                  : "carries a vector of " + std::to_string(*from_size) + " values into a port expecting " +
                        std::to_string(*to_size);
  const std::size_t components = std::max(from.components.size(), to.components.size());
  const auto unit_at = [](const PortDef& p, std::size_t i) -> std::string_view {
    if (i < p.components.size() && !p.components[i].unit.empty()) return p.components[i].unit;
    return p.unit;
  };
  for (std::size_t i = 0; i < components; ++i) {
    const auto from_unit = unit_at(from, i);
    const auto to_unit = unit_at(to, i);
    if (!from_unit.empty() && !to_unit.empty() && from_unit != to_unit)
      return std::string(points ? "carries point coordinate " : "carries vector component ") + std::to_string(i) + " in " +
             std::string(from_unit) + " into a port in " + std::string(to_unit);
  }
  // A list's one unit is that of every item: checked as a number's is. (A vector's, without
  // components, is not, as before.)
  if (components == 0 && (is_list_type(from.type) || is_list_type(to.type)) && !from.unit.empty() && !to.unit.empty() &&
      from.unit != to.unit)
    return "carries " + from.unit + " into a port in " + to.unit;
  return std::nullopt;
}
}  // namespace

std::optional<std::string> wire_mismatch(const PortDef& from, const PortDef& to) {
  const bool step_from = from.type == "step", step_to = to.type == "step";
  if (step_from != step_to) return "joins " + a_port(from.type) + " to " + a_port(to.type);
  // A broadcast port (an elementwise block's signal port) also takes, or gives, a vector.
  const auto vectorish = [](const PortDef& p) { return is_vector_type(p.type) || p.broadcast; };
  const bool same_type = from.type == to.type || (vectorish(from) && vectorish(to));
  if (!step_from && !same_type && from.type != "any" && to.type != "any")
    return "joins " + a_port(from.type) + " to " + a_port(to.type);
  if (vectorish(from) && vectorish(to)) return shape_mismatch(from, to, false);
  if (from.type == "points" && to.type == "points") return shape_mismatch(from, to, true);
  if (!from.unit.empty() && !to.unit.empty() && from.unit != to.unit) return "carries " + from.unit + " into a port in " + to.unit;
  return std::nullopt;
}

bool is_vector_type(std::string_view type) { return type == "vector" || type == "numbers"; }

bool is_list_type(std::string_view type) { return type == "numbers" || type == "points"; }

std::optional<std::size_t> vector_dimension(const PortDef& port) {
  if (port.dimension) return port.dimension;
  if (!port.components.empty()) return port.components.size();
  return std::nullopt;
}

PortDef vector_port(std::string id) { return {.id = std::move(id), .type = "vector"}; }

PortDef vector_port(std::string id, std::size_t dimension) {
  if (dimension == 0) throw std::invalid_argument("a vector dimension must be positive");
  return {.id = std::move(id), .type = "vector", .dimension = dimension};
}

PortDef vector_port(std::string id, std::vector<VectorComponent> components) {
  std::optional<std::size_t> dimension;
  if (!components.empty()) dimension = components.size();
  return {.id = std::move(id), .type = "vector", .dimension = dimension, .components = std::move(components)};
}

PortDef numbers_port(std::string id, std::string unit) { return {.id = std::move(id), .type = "numbers", .unit = std::move(unit)}; }

PortDef points_port(std::string id, std::vector<VectorComponent> coordinates) {
  std::optional<std::size_t> dimension;
  if (!coordinates.empty()) dimension = coordinates.size();
  return {.id = std::move(id), .type = "points", .dimension = dimension, .components = std::move(coordinates)};
}

PortDef matrix_port(std::string id) { return {.id = std::move(id), .type = "signal", .matrix = true}; }

std::optional<std::string> vector_value_mismatch(const PortDef& port, const Value& value) {
  if (port.matrix) {
    const auto* table = std::get_if<Table>(&value);
    if (!table) return "expects a matrix (rows of numbers)";
    if (table->empty()) return std::nullopt;
    const std::size_t columns = table->front().size();
    for (std::size_t i = 0; i < table->size(); ++i)
      if ((*table)[i].size() != columns)
        return "has row " + std::to_string(i) + " of " + std::to_string((*table)[i].size()) +
               " columns, expected " + std::to_string(columns);
    return std::nullopt;
  }
  if (port.type == "points") {
    if (const auto* list = std::get_if<Vector>(&value); list && list->empty()) return std::nullopt;  // no points
    const auto* table = std::get_if<Table>(&value);
    if (!table) return "expects a list of points";
    if (const auto dimension = vector_dimension(port))
      for (std::size_t i = 0; i < table->size(); ++i)
        if ((*table)[i].size() != *dimension)
          return "has point " + std::to_string(i) + " of " + std::to_string((*table)[i].size()) + " coordinates, expected " +
                 std::to_string(*dimension);
    return std::nullopt;
  }
  if (!is_vector_type(port.type)) return std::nullopt;
  const auto* vector = std::get_if<Vector>(&value);
  if (!vector) return "expects a vector value";
  if (const auto dimension = vector_dimension(port); dimension && vector->size() != *dimension)
    return "has " + std::to_string(vector->size()) + " values, expected " + std::to_string(*dimension);
  return std::nullopt;
}

namespace {

// See make_lanes.
class Lanes final : public Block {
 public:
  using Make = std::function<std::unique_ptr<Block>(const Values& params)>;
  Lanes(Make make, Values params, std::size_t lanes, std::vector<std::string> number_outputs)
      : make_(std::move(make)), params_(std::move(params)), n_(lanes), number_outputs_(std::move(number_outputs)) {
    if (n_ > 0) made(n_);
  }

  Values step(const Context& ctx, const Values& in) override {
    std::size_t n = 0;
    std::string first;
    for (const auto& [port, value] : in) {
      const auto* v = std::get_if<Vector>(&value);
      if (!v) continue;
      if (first.empty()) {
        n = v->size();
        first = port;
      } else if (v->size() != n) {
        throw std::invalid_argument("input " + port + " has " + std::to_string(v->size()) + " values, but " + first +
                                    " has " + std::to_string(n) + ": the vectors into an element-wise block are one length");
      }
    }
    if (first.empty()) n = lanes_.empty() ? std::max<std::size_t>(n_, 1) : lanes_.size();  // only numbers: each lane takes them
    if (n_ > 0 && n != n_)
      throw std::invalid_argument("input " + first + " has " + std::to_string(n) + " values, expected " + std::to_string(n_));
    if (n == 0) return empty(ctx, in);
    if (!lanes_.empty() && n != lanes_.size()) {
      // A list (#24) that changed length: lanes without state are made again for it.
      if (!std::ranges::all_of(lanes_, [](const auto& lane) { return lane->stateless(); }))
        throw std::invalid_argument("input " + first + " has " + std::to_string(n) + " values, but had " +
                                    std::to_string(lanes_.size()) + ": this block keeps a state per element, so the list "
                                    "into it can't change length during a run");
      lanes_.clear();
    }
    made(n);
    std::vector<Values> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      Values lane;
      for (const auto& [port, value] : in) {
        const auto* v = std::get_if<Vector>(&value);
        lane.emplace(port, v ? Value{(*v)[i]} : value);
      }
      out.push_back(lanes_[i]->step(ctx, lane));
    }
    return gathered(out);
  }

  Values outputs() const override {
    std::vector<Values> out;
    for (const auto& lane : lanes_) out.push_back(lane->outputs());
    return gathered(out);
  }

  std::vector<Entity> entities() const override {
    std::vector<Entity> out;
    for (const auto& lane : lanes_)
      for (auto& e : lane->entities()) out.push_back(std::move(e));
    return out;
  }

  // Its lanes are stateless (before they are made: one made to ask; one that can't be made
  // says no, and its first step reports why).
  bool stateless() const override {
    if (lanes_.empty()) {
      try {
        return make_(params_)->stateless();
      } catch (const std::exception&) {
        return false;
      }
    }
    return std::ranges::all_of(lanes_, [](const auto& lane) { return lane->stateless(); });
  }

  // Each lane takes it, or (a stateless one) is made again with it.
  bool set_param(std::string_view id, double value) override {
    auto params = params_;
    params.insert_or_assign(std::string(id), value);
    for (auto& lane : lanes_) {
      if (lane->set_param(id, value)) continue;
      if (!lane->stateless()) return false;
      lane = make_(params);
    }
    params_ = std::move(params);
    return true;
  }

 private:
  void made(std::size_t n) {
    if (!lanes_.empty()) return;
    for (std::size_t i = 0; i < n; ++i) lanes_.push_back(make_(params_));
  }

  // An empty list in: every number output is an empty list. The def says which outputs those
  // are; without it, a lane made for the purpose says (fed zeros for the lists), so the
  // block's own lanes keep their state.
  Values empty(const Context& ctx, const Values& in) const {
    if (!number_outputs_.empty()) {
      Values out;
      for (const auto& port : number_outputs_) out.emplace(port, Vector{});
      return out;
    }
    Values probe;
    for (const auto& [port, value] : in) probe.emplace(port, std::holds_alternative<Vector>(value) ? Value{0.0} : value);
    Values out = make_(params_)->step(ctx, probe);
    for (auto& [port, value] : out)
      if (std::holds_alternative<double>(value)) value = Vector{};
    return out;
  }

  // The lanes' outputs as one: numbers into a vector; anything else is lane 0's.
  static Values gathered(const std::vector<Values>& lanes) {
    Values out;
    if (lanes.empty()) return out;
    for (const auto& [port, first] : lanes.front()) {
      if (!std::holds_alternative<double>(first)) {
        out.emplace(port, first);
        continue;
      }
      Vector v;
      v.reserve(lanes.size());
      for (const auto& lane : lanes) v.push_back(get_or(lane, port));
      out.emplace(port, std::move(v));
    }
    return out;
  }

  Make make_;
  Values params_;
  std::size_t n_;  // 0: from the first step's vectors
  std::vector<std::string> number_outputs_;  // the def's number outputs, whose empty list is `[]`
  std::vector<std::unique_ptr<Block>> lanes_;
};

}  // namespace

std::unique_ptr<Block> make_lanes(std::function<std::unique_ptr<Block>(const Values& params)> make, Values params,
                                  std::size_t lanes, std::vector<std::string> number_outputs) {
  return std::make_unique<Lanes>(std::move(make), std::move(params), lanes, std::move(number_outputs));
}

BlockDef lifted(const BlockDef& def, std::size_t lanes, const std::vector<VectorComponent>& components) {
  BlockDef out = with_broadcast(def);
  for (auto& p : out.outputs) {
    if (p.type != "signal") continue;
    p.type = "vector";
    p.broadcast = false;
    if (lanes > 0) p.dimension = lanes;
    // The labels of the vector that came in; not its units: what a block makes of them
    // (an integral, a gain) is in other units. The output's own unit, if any, holds for all.
    if (lanes > 0 && components.size() == lanes)
      for (const auto& c : components) p.components.push_back({.id = c.id});
  }
  return out;
}

BlockDef with_broadcast(const BlockDef& def) {
  if (!def.elementwise) return def;
  BlockDef out = def;
  for (auto* side : {&out.inputs, &out.outputs})
    for (auto& p : *side)
      if (p.type == "signal") p.broadcast = true;
  return out;
}

Vector as_vector(const Values& values, std::string_view id) {
  auto it = values.find(id);
  if (it == values.end()) return {};
  if (const auto* v = std::get_if<Vector>(&it->second)) return *v;
  if (const auto* d = std::get_if<double>(&it->second)) return {*d};
  return {};
}

}  // namespace paperboat::toolkit
