#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::math {

inline nlohmann::json lerp_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{15, 70}, {85, 30}}}, {"stroke", "ink"}, {"weight", "normal"}},
      {{"t", "ellipse"}, {"x", 45}, {"y", 45}, {"w", 10}, {"h", 10}, {"fill", "ink"}}
    }}
  };
}

}  // namespace paperboat::toolkit::math
