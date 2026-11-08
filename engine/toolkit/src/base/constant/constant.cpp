// base/constant: "constant" for what it was called before it had siblings; shown as Number

#include <stdexcept>
#include <string>

#include "common.hpp"
#include "base/constant/icon.hpp"

namespace paperboat::toolkit::base {

void add_constant(Registry& registry) {
  // "constant" for what it was called before it had siblings; shown as Number.
  auto number = make_template(
      "base", "constant", "Number", {}, {"y"}, {{"value", 1.0}}, [](const Values& p, const Config&) {
        double value = get_or(p, "value");
        return std::make_unique<Stateless>([=](const Context&, const Values&) { return Values{{"y", value}}; });
      }, "A number you set.", {.compact = true, .glyph = "const", .value = "{value}", .icon = constant_icon()});
  registry.add(std::move(number));
}

}  // namespace paperboat::toolkit::base
