// math/distribution.

#include <algorithm>
#include <boost/math/distributions/exponential.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/uniform.hpp>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_distribution(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "distribution", "Distribution", {"u"}, {"y"},
      {{.id = "family", .type = "enum", .default_text = "normal", .options = {"normal", "uniform", "exponential"}},
       {.id = "function", .type = "enum", .default_text = "cdf", .options = {"pdf", "cdf", "quantile"}},
       {.id = "a", .default_value = 0.0, .type = "number"},
       {.id = "b", .default_value = 1.0, .type = "number"}},
      [](const Values& p, const Config&) {
        const int family = static_cast<int>(get_or(p, "family", 0.0));
        const int function = static_cast<int>(get_or(p, "function", 1.0));
        const double a = get_or(p, "a", 0.0);
        const double b = get_or(p, "b", 1.0);
        return std::make_unique<Stateless>([family, function, a, b](const Context&, const Values& in) {
          // a, b are mean and standard deviation (normal), lower and upper (uniform),
          // or the rate (exponential). Bad ranges fall back to a unit distribution.
          const double u = get_or(in, "u");
          const double x = std::clamp(u, 0.0, 1.0);
          double y = 0.0;
          switch (family) {
            case 1: {
              const double lo = std::min(a, b), hi = std::max(a, b);
              const boost::math::uniform_distribution<double> d(lo, lo == hi ? lo + 1.0 : hi);
              y = function == 0 ? pdf(d, u) : function == 2 ? quantile(d, x) : cdf(d, u);
              break;
            }
            case 2: {
              const boost::math::exponential_distribution<double> d(a > 0.0 ? a : 1.0);
              y = function == 0 ? pdf(d, u) : function == 2 ? quantile(d, x) : cdf(d, u);
              break;
            }
            default: {
              const boost::math::normal_distribution<double> d(a, b > 0.0 ? b : 1.0);
              y = function == 0 ? pdf(d, u) : function == 2 ? quantile(d, x) : cdf(d, u);
              break;
            }
          }
          return Values{{"y", y}};
        });
      },
      "The pdf, cdf or quantile of a normal, uniform or exponential distribution (Boost.Math).",
      {.compact = true, .glyph = "cdf", .value = "{family}/{function}"}));
}

}  // namespace paperboat::toolkit::math
