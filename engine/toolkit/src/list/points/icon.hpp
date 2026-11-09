#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::list {

// Scattered dots over two axes: a list of points.
inline nlohmann::json points_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "poly"}, {"p", {{10, 10}, {10, 90}, {90, 90}}}, {"stroke", "muted"}},
      {{"t", "ellipse"}, {"x", 25}, {"y", 60}, {"w", 10}, {"h", 10}, {"fill", "ink"}},
      {{"t", "ellipse"}, {"x", 45}, {"y", 30}, {"w", 10}, {"h", 10}, {"fill", "ink"}},
      {{"t", "ellipse"}, {"x", 70}, {"y", 50}, {"w", 10}, {"h", 10}, {"fill", "ink"}}
    }}
  };
}

}  // namespace paperboat::toolkit::list
