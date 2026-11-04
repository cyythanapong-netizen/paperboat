#include "paperboat/toolkit/block.hpp"

#include "paperboat/toolkit/station.hpp"

#include <algorithm>
#include <stdexcept>

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

BlockDef with_ports(const BlockDef& def, const Config& text) {
  if (def.inputs_by.empty()) return def;
  auto it = text.find(def.inputs_by);
  const std::string value = it == text.end() ? std::string() : it->second;
  if (value.empty() || value.size() > 26)
    throw std::runtime_error("`" + def.inputs_by + "` needs 1 to 26 characters, one per input, not " +
                             (value.empty() ? std::string("none") : std::to_string(value.size())));
  BlockDef out = def;
  const std::string type = def.inputs.empty() ? "signal" : def.inputs.front().type;
  out.inputs.clear();
  for (std::size_t i = 0; i < value.size(); ++i) out.inputs.push_back({std::string(1, static_cast<char>('a' + i)), type});
  return out;
}

bool has_step_ports(const BlockDef& def) {
  auto step = [](const PortDef& p) { return p.type == "step"; };
  return std::ranges::any_of(def.inputs, step) || std::ranges::any_of(def.outputs, step);
}

}  // namespace paperboat::toolkit
