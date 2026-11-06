#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/station.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
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

BlockDef with_dynamic_ports(const BlockDef& def, const Values& params, const Config& text) {
  if (def.ports_from.empty()) return def;
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
    count = std::clamp(count, 2, 26);
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

BlockDef with_signs(const BlockDef& def, std::string_view text) {
  if (def.ports_from.empty()) return def;
  Config cfg;
  cfg[def.ports_from] = std::string(text);
  return with_dynamic_ports(def, {}, cfg);
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
