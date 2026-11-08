// base/in_range.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "base/in_range/icon.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_in_range(Registry& registry) {
  add_test(
      registry, "in_range", "In Range", {"u", "min", "max"},
      {{.id = "min", .default_value = 0.0, .type = "number"},
       {.id = "max", .default_value = 1.0, .type = "number"},
       {.id = "inclusive_min", .default_value = 1.0, .type = "boolean"},
       {.id = "inclusive_max", .default_value = 1.0, .type = "boolean"}},
      [](const Values& in, const Values& p) {
        const double u = get_or(in, "u");
        const double lo = in.contains("min") ? get_or(in, "min") : get_or(p, "min", 0.0);
        const double hi = in.contains("max") ? get_or(in, "max") : get_or(p, "max", 1.0);
        const bool inc_min = get_or(p, "inclusive_min", 1.0) != 0.0;
        const bool inc_max = get_or(p, "inclusive_max", 1.0) != 0.0;
        const bool above = inc_min ? (u >= lo) : (u > lo);
        const bool below = inc_max ? (u <= hi) : (u < hi);
        return above && below;
      },
      "Holds when u is between min and max.", {.compact = true, .glyph = "window", .value = "{min}…{max}", .icon = window_icon()});
}

}  // namespace paperboat::toolkit::base
