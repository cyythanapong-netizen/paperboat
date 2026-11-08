// math/round.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/round/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_round(Registry& registry) {
  // math/round
  add_pointwise(registry, make_template(
      "math", "round", "Round", {"u"}, {"y"},
      {{.id = "mode", .type = "enum", .default_text = "round", .options = {"round", "floor", "ceil", "truncate"}}},
      [](const Values& p, const Config&) {
        int mode = static_cast<int>(get_or(p, "mode", 0.0));
        return std::make_unique<Stateless>([mode](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double y = 0.0;
          switch (mode) {
            case 0: y = std::round(u); break;
            case 1: y = std::floor(u); break;
            case 2: y = std::ceil(u); break;
            case 3: y = std::trunc(u); break;
            default: y = std::round(u); break;
          }
          return Values{{"y", y}};
        });
      },
      "Rounds u to an integer using the chosen mode.", {.compact = true, .glyph = "staircase", .value = "{mode}", .icon = staircase_icon()}));
}

}  // namespace paperboat::toolkit::math
