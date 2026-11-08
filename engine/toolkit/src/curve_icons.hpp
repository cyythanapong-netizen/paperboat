#pragma once

#include <nlohmann/json.hpp>

namespace paperboat::toolkit::icons {

// Declarative view.icon definitions for standard compact blocks (§3, tokenized primitives on 5-grid).

inline nlohmann::json gain() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}, {"dash", "axis"}},
      {{"t", "line"}, {"p", {{15, 80}, {85, 20}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json plus() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{50, 20}, {50, 80}}}, {"stroke", "ink"}, {"weight", "normal"}},
      {{"t", "line"}, {"p", {{20, 50}, {80, 50}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json step() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 70}, {50, 70}, {50, 25}, {85, 25}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json clamp() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {30, 75}, {70, 25}, {85, 25}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json constant() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{15, 40}, {85, 40}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json abs() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 25}, {50, 75}, {85, 25}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json staircase() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 50}, {90, 50}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 80}, {35, 80}, {35, 55}, {65, 55}, {65, 30}, {85, 30}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json window() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {30, 75}, {30, 35}, {70, 35}, {70, 75}, {85, 75}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json pulse() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "poly"}, {"p", {{15, 75}, {25, 75}, {25, 35}, {45, 35}, {45, 75}, {55, 75}, {55, 35}, {75, 35}, {75, 75}, {85, 75}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json timer() {
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

inline nlohmann::json ramp() {
  return {
    {"w", 1},
    {"h", 1},
    {"g", {
      {{"t", "line"}, {"p", {{10, 80}, {90, 80}}}, {"stroke", "muted"}},
      {{"t", "line"}, {"p", {{15, 70}, {85, 25}}}, {"stroke", "ink"}, {"weight", "normal"}}
    }}
  };
}

inline nlohmann::json lerp() {
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

}  // namespace paperboat::toolkit::icons
