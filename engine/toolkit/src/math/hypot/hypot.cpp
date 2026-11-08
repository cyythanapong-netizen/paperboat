// math/hypot.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_hypot(Registry& registry) {
  // math/hypot
  add_pointwise(registry, make_template(
      "math", "hypot", "Hypot", {"a", "b"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = in.contains("a") ? get_or(in, "a") : get_or(in, "x", 0.0);
          double b = in.contains("b") ? get_or(in, "b") : get_or(in, "y", 0.0);
          return Values{{"y", std::hypot(a, b)}};
        });
      },
      "Computes the Euclidean distance hypot(a, b) = sqrt(a*a + b*b).", {.compact = true, .glyph = "hypot"}));
}

}  // namespace paperboat::toolkit::math
