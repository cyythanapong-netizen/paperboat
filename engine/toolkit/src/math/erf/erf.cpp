// math/erf.

#include <boost/math/special_functions/erf.hpp>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_erf(Registry& registry) {
  add_pointwise(registry, make_template(
      "math", "erf", "Error Function", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", boost::math::erf(get_or(in, "u"))}};
        });
      },
      "The error function of u (Boost.Math).", {.compact = true, .glyph = "erf"}));
}

}  // namespace paperboat::toolkit::math
