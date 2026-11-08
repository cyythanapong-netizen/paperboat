#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::math {

inline nlohmann::json staircase_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 80}, {35, 80}, {35, 55}, {65, 55}, {65, 30}, {85, 30}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::math
