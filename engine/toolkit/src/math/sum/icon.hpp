#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::math {

inline nlohmann::json plus_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{50, 20}, {50, 80}}}, {"stroke", "ink"}, {"weight", "normal"}},
      {{"t", "line"}, {"p", {{20, 50}, {80, 50}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::math
