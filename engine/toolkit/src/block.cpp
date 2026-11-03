#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/station.hpp"

#include <algorithm>

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

bool has_step_ports(const BlockDef& def) {
  auto step = [](const PortDef& p) { return p.type == "step"; };
  return std::ranges::any_of(def.inputs, step) || std::ranges::any_of(def.outputs, step);
}

}  // namespace paperboat::toolkit
