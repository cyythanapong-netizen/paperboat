// math/saturation.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/clamp/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_saturation(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "saturation", "Saturation", {"u"}, {"y"}, {{"min", -1.0}, {"max", 1.0}},
      [](const Values& p, const Config&) {
        double lo = get_or(p, "min"), hi = get_or(p, "max");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          return Values{{"y", std::min(std::max(get_or(in, "u"), lo), hi)}};
        });
      },
      "Keeps its input between a minimum and a maximum.", {.compact = true, .glyph = "clamp", .value = "{min}…{max}", .icon = clamp_icon()}));
}

}  // namespace paperboat::toolkit::math
