// math/threshold.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/threshold/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_threshold(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "threshold", "Threshold", {"u"}, {"y"}, {{"level", 0.5}},
      [](const Values& p, const Config&) {
        double level = get_or(p, "level");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") > level ? 1.0 : 0.0}};
        });
      },
      "1 above a level, else 0.", {.compact = true, .glyph = "step", .value = "{level}", .icon = step_icon()}));
}

}  // namespace paperboat::toolkit::math
