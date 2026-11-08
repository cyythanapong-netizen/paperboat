// math/clamp.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/clamp/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_clamp(Registry& registry) {
  // math/clamp
  add_pointwise(registry, make_template(
      "math", "clamp", "Clamp", {"u", "min", "max"}, {"y"},
      {{.id = "min", .default_value = 0.0, .type = "number"},
       {.id = "max", .default_value = 1.0, .type = "number"}},
      [](const Values& p, const Config&) {
        double p_lo = get_or(p, "min", 0.0), p_hi = get_or(p, "max", 1.0);
        return std::make_unique<Stateless>([p_lo, p_hi](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double lo = in.contains("min") ? get_or(in, "min") : p_lo;
          double hi = in.contains("max") ? get_or(in, "max") : p_hi;
          if (lo > hi) std::swap(lo, hi);
          return Values{{"y", std::clamp(u, lo, hi)}};
        });
      },
      "Restricts u between min and max.", {.compact = true, .glyph = "clamp", .value = "{min}…{max}", .icon = clamp_icon()}));
}

}  // namespace paperboat::toolkit::math
