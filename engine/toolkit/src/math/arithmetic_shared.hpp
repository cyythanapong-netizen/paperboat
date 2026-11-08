#pragma once

// Shared by several math blocks.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"

namespace paperboat::toolkit::math {

// Pointwise: each takes vectors too, element by element (#164).
inline void add_pointwise(Registry& registry, Template t) {
  t.def.elementwise = true;
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
