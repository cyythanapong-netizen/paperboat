// math/angle_diff.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_angle_diff(Registry& registry) {
  // math/angle_diff
  add_pointwise(registry, make_template(
      "math", "angle_diff", "Angle Diff", {"a", "b"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b");
          return Values{{"y", std::atan2(std::sin(a - b), std::cos(a - b))}};
        });
      },
      "Computes the shortest signed difference (a - b) normalized to [-pi, pi].", {.compact = true, .glyph = "Δ∠"}));
}

}  // namespace paperboat::toolkit::math
