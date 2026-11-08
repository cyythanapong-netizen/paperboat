#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::math {

inline nlohmann::json clamp_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {30, 75}, {70, 25}, {85, 25}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::math
