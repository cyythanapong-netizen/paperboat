// base/boolean.

#include <stdexcept>
#include <string>

#include "common.hpp"

namespace paperboat::toolkit::base {

void add_boolean(Registry& registry) {
  registry.add(make_template(
      "base", "boolean", "Boolean", {}, {"y"}, {{.id = "value", .default_value = 1, .type = "boolean"}},
      [](const Values& p, const Config&) {
        const double value = get_or(p, "value") != 0 ? 1.0 : 0.0;
        return std::make_unique<Stateless>([=](const Context&, const Values&) { return Values{{"y", value}}; });
      }, "On or off: 1 or 0."));
}

}  // namespace paperboat::toolkit::base
