#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/station.hpp"

#include <algorithm>
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
  if (!step_from && from.type != to.type && from.type != "any" && to.type != "any")
    return "joins " + a_port(from.type) + " to " + a_port(to.type);
  if (!from.unit.empty() && !to.unit.empty() && from.unit != to.unit)
    return "carries " + from.unit + " into a port in " + to.unit;
  return std::nullopt;
}

}  // namespace paperboat::toolkit
