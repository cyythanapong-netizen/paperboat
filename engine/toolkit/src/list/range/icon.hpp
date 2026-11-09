#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::list {

// Evenly spaced dots on a rising line: numbers from start to stop.
inline nlohmann::json range_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 20}}}, {"stroke", "muted"}, {"dash", "axis"}},
      {{"t", "ellipse"}, {"x", 10}, {"y", 70}, {"w", 10}, {"h", 10}, {"fill", "ink"}},
      {{"t", "ellipse"}, {"x", 35}, {"y", 50}, {"w", 10}, {"h", 10}, {"fill", "ink"}},
      {{"t", "ellipse"}, {"x", 55}, {"y", 35}, {"w", 10}, {"h", 10}, {"fill", "ink"}},
      {{"t", "ellipse"}, {"x", 80}, {"y", 15}, {"w", 10}, {"h", 10}, {"fill", "ink"}}
    }}
  };
}

}  // namespace paperboat::toolkit::list
