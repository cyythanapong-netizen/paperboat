#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::base {

inline nlohmann::json constant_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{15, 40}, {85, 40}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

}  // namespace paperboat::toolkit::base
