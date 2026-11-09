// math/gamma.

#include <boost/math/special_functions/digamma.hpp>
#include <boost/math/special_functions/gamma.hpp>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_gamma(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "gamma", "Gamma", {"u"}, {"y"},
      {{.id = "mode", .type = "enum", .default_text = "gamma", .options = {"gamma", "log", "digamma"}}},
      [](const Values& p, const Config&) {
        const int mode = static_cast<int>(get_or(p, "mode", 0.0));
        return std::make_unique<Stateless>([mode](const Context&, const Values& in) {
          const double u = get_or(in, "u");
          double y = 0.0;
          switch (mode) {
            case 1: y = boost::math::lgamma(u); break;
            case 2: y = boost::math::digamma(u); break;
            default: y = boost::math::tgamma(u); break;
          }
          return Values{{"y", y}};
        });
      },
      "The gamma, log-gamma or digamma function of u (Boost.Math).",
      {.compact = true, .glyph = "Γ", .value = "{mode}"}));
}

}  // namespace paperboat::toolkit::math
