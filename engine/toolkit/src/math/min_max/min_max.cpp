// math/min_max.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_min_max(Registry& registry) {
  // math/min_max
  auto min_max = make_template(
      "math", "min_max", "Min / Max", {"a", "b"}, {"y"},
      {{.id = "mode", .type = "enum", .default_text = "min", .options = {"min", "max"}},
       {.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& p, const Config&) {
        int mode = static_cast<int>(get_or(p, "mode", 0.0));
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 1, 26);
        return std::make_unique<Stateless>([mode, n](const Context&, const Values& in) {
          double res = get_or(in, "a");
          for (int i = 1; i < n; ++i) {
            double v = get_or(in, std::string(1, static_cast<char>('a' + i)));
            res = (mode == 0) ? std::min(res, v) : std::max(res, v);
          }
          return Values{{"y", res}};
        });
      },
      "Selects the minimum or maximum among inputs.", {.compact = true, .glyph = "{mode}", .value = "{mode}"});
  min_max.def.ports_from = "inputs";
  add_pointwise(registry, std::move(min_max));
}

}  // namespace paperboat::toolkit::math
