// math/atan2.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_atan2(Registry& registry) {
  // math/atan2
  add_pointwise(registry, make_template(
      "math", "atan2", "Atan2", {"y", "x"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double y = get_or(in, "y"), x = get_or(in, "x");
          return Values{{"y", std::atan2(y, x)}};
        });
      },
      "Computes the 2-argument arctangent atan2(y, x) in radians.", {.compact = true, .glyph = "atan2"}));
}

}  // namespace paperboat::toolkit::math
