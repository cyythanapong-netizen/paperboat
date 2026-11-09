#pragma once

// The block's look on its plate: a declarative view.icon (tokenized primitives on a 5-grid).
#include <nlohmann/json.hpp>

namespace paperboat::toolkit::dashboard {

// A round lamp on a socket, with short rays. Drawn in ink and grey only: whether it is lit is
// live state, which the plate and the Dashboard show, never the icon.
inline nlohmann::json lamp_icon() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 85}, {90, 85}}}, {"stroke", "muted"}},
      {{"t", "rect"}, {"x", 40}, {"y", 65}, {"w", 20}, {"h", 15}, {"corner", "sm"}, {"stroke", "muted"}},
      {{"t", "ellipse"}, {"x", 30}, {"y", 10}, {"w", 40}, {"h", 50}, {"stroke", "ink"}, {"weight", "normal"}},
      {{"t", "line"}, {"p", {{10, 35}, {20, 35}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{80, 35}, {90, 35}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{15, 5}, {25, 15}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{85, 5}, {75, 15}}}, {"stroke", "muted"}}
    }}
  };
}

}  // namespace paperboat::toolkit::dashboard
