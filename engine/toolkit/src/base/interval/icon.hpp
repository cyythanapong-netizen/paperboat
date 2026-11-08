#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::base {

inline nlohmann::json pulse_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {25, 75}, {25, 35}, {45, 35}, {45, 75}, {55, 75}, {55, 35}, {75, 35}, {75, 75}, {85, 75}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::base
