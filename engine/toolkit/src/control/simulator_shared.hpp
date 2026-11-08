#pragma once

// Shared by several control blocks.

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "common.hpp"
#include "paperboat/toolkit/control/simulator.hpp"
#include "paperboat/toolkit/solve.hpp"

namespace paperboat::toolkit::control {

// A dynamic block: no direct feedthrough; a vector gets one state per element (Delay moves whole values).
inline void add_without_feedthrough(Registry& registry, Template t) {
  t.def.feedthrough = false;
  t.def.elementwise = t.def.type != "delay";
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::control
