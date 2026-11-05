#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/station.hpp"

#include <algorithm>
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

BlockDef with_signs(const BlockDef& def, std::string_view text) {
  if (def.ports_from.empty()) return def;
  BlockDef out = def;
  const PortDef proto = def.inputs.empty() ? PortDef{} : def.inputs.front();
  out.inputs.clear();
  for (std::size_t i = 0; i < parse_signs(text).size(); ++i) {
    PortDef p = proto;
    p.id = std::string(1, static_cast<char>('a' + i));
    out.inputs.push_back(std::move(p));
  }
  return out;
}

bool has_step_ports(const BlockDef& def) {
  auto step = [](const PortDef& p) { return p.type == "step"; };
  return std::ranges::any_of(def.inputs, step) || std::ranges::any_of(def.outputs, step);
}

namespace {
std::string a_port(const std::string& type) {
  return std::string(std::string_view("aeiou").find(type.empty() ? 'x' : type[0]) != std::string_view::npos ? "an " : "a ") + type + " port";
}
}  // namespace

std::optional<std::string> wire_mismatch(const PortDef& from, const PortDef& to) {
  const bool step_from = from.type == "step", step_to = to.type == "step";
  if (step_from != step_to) return "joins " + a_port(from.type) + " to " + a_port(to.type);
  const bool same_type = from.type == to.type || (is_vector_type(from.type) && is_vector_type(to.type));
  if (!step_from && !same_type && from.type != "any" && to.type != "any")
    return "joins " + a_port(from.type) + " to " + a_port(to.type);
  if (is_vector_type(from.type) && is_vector_type(to.type)) {
    const auto from_size = vector_dimension(from);
    const auto to_size = vector_dimension(to);
    if (from_size && to_size && *from_size != *to_size)
      return "carries a vector of " + std::to_string(*from_size) + " values into a port expecting " +
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
        return "carries vector component " + std::to_string(i) + " in " + std::string(from_unit) +
               " into a port in " + std::string(to_unit);
    }
  } else if (!from.unit.empty() && !to.unit.empty() && from.unit != to.unit) {
    return "carries " + from.unit + " into a port in " + to.unit;
  }
  return std::nullopt;
}

bool is_vector_type(std::string_view type) { return type == "vector" || type == "numbers"; }

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

std::optional<std::string> vector_value_mismatch(const PortDef& port, const Value& value) {
  if (!is_vector_type(port.type)) return std::nullopt;
  const auto* vector = std::get_if<Vector>(&value);
  if (!vector) return "expects a vector value";
  if (const auto dimension = vector_dimension(port); dimension && vector->size() != *dimension)
    return "has " + std::to_string(vector->size()) + " values, expected " + std::to_string(*dimension);
  return std::nullopt;
}

}  // namespace paperboat::toolkit
