// math/deadband.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_deadband(Registry& registry) {
  // math/deadband
  add_pointwise(registry, make_template(
      "math", "deadband", "Deadband", {"u"}, {"y"},
      {{.id = "width", .default_value = 0.1, .type = "number"}},
      [](const Values& p, const Config&) {
        double width = get_or(p, "width", 0.1);
        return std::make_unique<Stateless>([width](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double y = 0.0;
          if (std::abs(u) > width) {
            y = (u > 0.0) ? (u - width) : (u + width);
          }
          return Values{{"y", y}};
        });
      },
      "Suppresses signals within [-width, +width].", {.compact = true, .glyph = "deadband", .value = "{width}"}));
}

}  // namespace paperboat::toolkit::math
