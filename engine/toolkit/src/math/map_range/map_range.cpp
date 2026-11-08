// math/map_range.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/map_range/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_map_range(Registry& registry) {
  // math/map_range
  add_pointwise(registry, make_template(
      "math", "map_range", "Map Range", {"u", "in_min", "in_max", "out_min", "out_max"}, {"y"},
      {{.id = "in_min", .default_value = 0.0, .type = "number"},
       {.id = "in_max", .default_value = 1.0, .type = "number"},
       {.id = "out_min", .default_value = 0.0, .type = "number"},
       {.id = "out_max", .default_value = 100.0, .type = "number"},
       {.id = "clamp", .default_value = 0.0, .type = "boolean"}},
      [](const Values& p, const Config&) {
        double p_in_min = get_or(p, "in_min", 0.0), p_in_max = get_or(p, "in_max", 1.0);
        double p_out_min = get_or(p, "out_min", 0.0), p_out_max = get_or(p, "out_max", 100.0);
        bool clamp_val = get_or(p, "clamp", 0.0) != 0.0;
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double in_lo = in.contains("in_min") ? get_or(in, "in_min") : p_in_min;
          double in_hi = in.contains("in_max") ? get_or(in, "in_max") : p_in_max;
          double out_lo = in.contains("out_min") ? get_or(in, "out_min") : p_out_min;
          double out_hi = in.contains("out_max") ? get_or(in, "out_max") : p_out_max;
          if (in_lo == in_hi) return Values{{"y", out_lo}};
          double t = (u - in_lo) / (in_hi - in_lo);
          if (clamp_val) t = std::clamp(t, 0.0, 1.0);
          return Values{{"y", out_lo + t * (out_hi - out_lo)}};
        });
      },
      "Maps u linearly from [in_min, in_max] to [out_min, out_max].",
      {.compact = true, .glyph = "ramp", .value = "{out_min}…{out_max}", .icon = ramp_icon()}));
}

}  // namespace paperboat::toolkit::math
