// math/wrap_angle.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_wrap_angle(Registry& registry) {
  // math/wrap_angle
  add_pointwise(registry, make_template(
      "math", "wrap_angle", "Wrap Angle", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double u = get_or(in, "u");
          return Values{{"y", std::atan2(std::sin(u), std::cos(u))}};
        });
      },
      "Wraps an angle in radians into the range [-pi, pi].", {.compact = true, .glyph = "wrap∠"}));
}

}  // namespace paperboat::toolkit::math
