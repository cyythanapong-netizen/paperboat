#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::base {

inline nlohmann::json timer_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {55, 75}, {55, 30}, {85, 30}}}, {"stroke", "ink"}, {"weight", "normal"}},
      {{"t", "line"}, {"p", {{55, 30}, {55, 80}}}, {"stroke", "muted"}, {"dash", "axis"}}
    }}
  };
}

}  // namespace paperboat::toolkit::base
