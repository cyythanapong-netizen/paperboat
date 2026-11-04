// The math package: pointwise arithmetic on signals.

#include <algorithm>
#include <stdexcept>

#include "common.hpp"

namespace paperboat::toolkit::math {

void add_arithmetic(Registry& registry) {
  registry.add(make_template(
      "math", "gain", "Gain", {"u"}, {"y"}, {{"k", 1.0}}, [](const Values& p, const Config&) {
        double k = get_or(p, "k");
        return std::make_unique<Stateless>(
            [=](const Context&, const Values& in) { return Values{{"y", k * get_or(in, "u")}}; });
      }));

  // Sum adds or subtracts any number of inputs: `signs` has a character per input, "+" adds
  // and "-" subtracts, so "+-+" is a - b + c. Its inputs are a, b, c… (see BlockDef::inputs_by).
  Template sum = make_template(
      "math", "sum", "Sum", {"a", "b"}, {"y"}, {{.id = "signs", .type = "text", .default_text = "++"}},
      [](const Values&, const Config& text) {
        std::vector<double> sign;
        const auto given = text.find("signs");  // absent when made without its params: the default
        for (char c : given == text.end() ? std::string("++") : given->second) {
          if (c != '+' && c != '-') throw std::runtime_error("signs holds + and -, one per input, not \"" + std::string(1, c) + "\"");
          sign.push_back(c == '+' ? 1.0 : -1.0);
        }
        return std::make_unique<Stateless>([sign](const Context&, const Values& in) {
          double y = 0;
          for (std::size_t i = 0; i < sign.size(); ++i) y += sign[i] * get_or(in, std::string(1, static_cast<char>('a' + i)));
          return Values{{"y", y}};
        });
      });
  sum.def.inputs_by = "signs";
  registry.add(std::move(sum));

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
