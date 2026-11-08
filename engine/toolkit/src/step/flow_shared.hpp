#pragma once

// Shared by several step blocks.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"

namespace paperboat::toolkit::step {

inline PortDef step_port(std::string id) { return {std::move(id), "step"}; }

inline Template make_step_template(std::string type, std::string label, std::vector<PortDef> inputs,
                            std::vector<PortDef> outputs, std::vector<ParamDef> params,
                            decltype(Template::make_step) make, std::string description) {
  Template t;
  t.name = "step/" + type;
  t.def.type = std::move(type);
  t.def.label = std::move(label);
  t.def.kind = Kind::Function;
  t.def.inputs = std::move(inputs);
  t.def.outputs = std::move(outputs);
  t.def.params = std::move(params);
  t.def.description = std::move(description);
  t.make_step = std::move(make);
  return t;
}

}  // namespace paperboat::toolkit::step
