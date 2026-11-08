// math/rad_to_deg.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_rad_to_deg(Registry& registry) {
  // math/rad_to_deg
  add_pointwise(registry, make_template(
      "math", "rad_to_deg", "Rad to Deg", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") * (180.0 / std::numbers::pi)}};
        });
      },
      "Converts an angle from radians to degrees.", {.compact = true, .glyph = "rad→°"}));
}

}  // namespace paperboat::toolkit::math
