// math/absolute.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/absolute/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_absolute(Registry& registry) {
  // math/absolute
  add_pointwise(registry, make_template(
      "math", "absolute", "Absolute", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", std::abs(get_or(in, "u"))}};
        });
      },
      "Computes the absolute value of u.", {.compact = true, .glyph = "abs", .icon = abs_icon()}));
}

}  // namespace paperboat::toolkit::math
