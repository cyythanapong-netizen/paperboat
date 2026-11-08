// math/arithmetic.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_arithmetic(Registry& registry) {
  // math/arithmetic
  add_pointwise(registry, make_template(
      "math", "arithmetic", "Arithmetic", {"a", "b"}, {"y"},
      {{.id = "op", .type = "enum", .default_text = "+", .options = {"+", "-", "*", "/", "%", "^"}}},
      [](const Values& p, const Config&) {
        int op = static_cast<int>(get_or(p, "op", 0.0));
        return std::make_unique<Stateless>([op](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b");
          double y = 0.0;
          switch (op) {
            case 0: y = a + b; break;
            case 1: y = a - b; break;
            case 2: y = a * b; break;
            case 3: y = (b == 0.0) ? 0.0 : (a / b); break;
            case 4: y = (b == 0.0) ? 0.0 : std::fmod(a, b); break;
            case 5: y = std::pow(a, b); break;
            default: y = a + b; break;
          }
          return Values{{"y", y}};
        });
      },
      "Applies an arithmetic operator to a and b.", {.compact = true, .glyph = "{op}", .value = "{op}"}));
}

}  // namespace paperboat::toolkit::math
