// The math package: pointwise arithmetic on signals.

#include <algorithm>

#include "common.hpp"

namespace paperboat::toolkit::math {

void add_arithmetic(Registry& registry) {
  registry.add(make_template(
      "math", "gain", "Gain", {"u"}, {"y"}, {{"k", 1.0}}, [](const Values& p, const Config&) {
        double k = get_or(p, "k");
        return std::make_unique<Stateless>(
            [=](const Context&, const Values& in) { return Values{{"y", k * get_or(in, "u")}}; });
      }));

  registry.add(make_template(
      "math", "sum", "Sum", {"a", "b"}, {"y"}, {}, [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "a") + get_or(in, "b")}};
        });
      }));

  registry.add(make_template(
      "math", "threshold", "Threshold", {"u"}, {"y"}, {{"level", 0.5}}, [](const Values& p, const Config&) {
        double level = get_or(p, "level");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") > level ? 1.0 : 0.0}};
        });
      }));

  registry.add(make_template(
      "math", "saturation", "Saturation", {"u"}, {"y"}, {{"min", -1.0}, {"max", 1.0}},
      [](const Values& p, const Config&) {
        double lo = get_or(p, "min"), hi = get_or(p, "max");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          // std::clamp is undefined when lo > hi; a misconfigured block shouldn't crash a run.
          return Values{{"y", std::min(std::max(get_or(in, "u"), lo), hi)}};
        });
      }));
}

}  // namespace paperboat::toolkit::math
