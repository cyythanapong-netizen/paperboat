// math/gain.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/gain/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_gain(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "gain", "Gain", {"u"}, {"y"}, {{"k", 1.0}},
      [](const Values& p, const Config&) {
        const double k = get_or(p, "k");
        return std::make_unique<Stateless>(
            [=](const Context&, const Values& in) { return Values{{"y", k * get_or(in, "u")}}; });
      },
      "Multiplies its input by k.", {.compact = true, .glyph = "gain", .value = "{k}", .icon = gain_icon()}));
}

}  // namespace paperboat::toolkit::math
