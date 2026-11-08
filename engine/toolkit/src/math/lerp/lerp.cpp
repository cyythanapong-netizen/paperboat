// math/lerp.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/lerp/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_lerp(Registry& registry) {
  // math/lerp
  add_pointwise(registry, make_template(
      "math", "lerp", "Lerp", {"a", "b", "t"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b"), t = get_or(in, "t");
          return Values{{"y", a + t * (b - a)}};
        });
      },
      "Linearly interpolates between a and b by factor t.", {.compact = true, .glyph = "lerp", .icon = lerp_icon()}));
}

}  // namespace paperboat::toolkit::math
