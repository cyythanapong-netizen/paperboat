// math/deg_to_rad.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_deg_to_rad(Registry& registry) {
  // math/deg_to_rad
  add_pointwise(registry, make_template(
      "math", "deg_to_rad", "Deg to Rad", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") * (std::numbers::pi / 180.0)}};
        });
      },
      "Converts an angle from degrees to radians.", {.compact = true, .glyph = "°→rad"}));
}

}  // namespace paperboat::toolkit::math
