// base/boolean.

#include <stdexcept>
#include <string>

#include "common.hpp"

namespace paperboat::toolkit::base {

void add_boolean(Registry& registry) {
  auto t = make_template(
      "base", "boolean", "Boolean", {}, {"y"}, {{.id = "value", .default_value = 1, .type = "boolean"}},
      [](const Values& p, const Config&) {
        const double value = get_or(p, "value") != 0 ? 1.0 : 0.0;
        return std::make_unique<Stateless>([=](const Context&, const Values&) { return Values{{"y", value}}; });
      }, "On or off: 1 or 0.");
  t.def.view = {.compact = true, .value = "{value}", .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"rect","x":25,"y":35,"w":50,"h":30,"corner":"md"},{"t":"ellipse","x":50,"y":40,"w":20,"h":20,"fill":"ink"}]})")};  // a toggle
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::base
