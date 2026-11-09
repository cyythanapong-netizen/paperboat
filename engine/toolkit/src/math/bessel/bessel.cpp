// math/bessel.

#include <algorithm>
#include <boost/math/special_functions/bessel.hpp>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_bessel(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "bessel", "Bessel", {"u"}, {"y"},
      {{.id = "order", .default_value = 0.0, .type = "number"},
       {.id = "kind", .type = "enum", .default_text = "J", .options = {"J", "Y", "I", "K"}}},
      [](const Values& p, const Config&) {
        const double order = std::max(0.0, get_or(p, "order", 0.0));
        const int kind = static_cast<int>(get_or(p, "kind", 0.0));
        return std::make_unique<Stateless>([order, kind](const Context&, const Values& in) {
          const double u = get_or(in, "u");
          double y = 0.0;
          switch (kind) {
            case 1: y = boost::math::cyl_neumann(order, u); break;  // Y
            case 2: y = boost::math::cyl_bessel_i(order, u); break;
            case 3: y = boost::math::cyl_bessel_k(order, u); break;
            default: y = boost::math::cyl_bessel_j(order, u); break;  // J
          }
          return Values{{"y", y}};
        });
      },
      "The Bessel function of u, of the chosen kind and order (Boost.Math).",
      {.compact = true, .glyph = "Jν", .value = "{kind}{order}"}));
}

}  // namespace paperboat::toolkit::math
