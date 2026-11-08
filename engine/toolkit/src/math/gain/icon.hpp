#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::math {

inline nlohmann::json gain_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}, {"dash", "axis"}},
      {{"t", "line"}, {"p", {{15, 80}, {85, 20}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::math
