// math/sum: Sum: one input per sign in `signs` ("+-+": a − b + c), ids a, b, c…

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/sum/icon.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {

void add_sum(Registry& registry) {
  // Sum: one input per sign in `signs` ("+-+": a − b + c), ids a, b, c…
  auto sum = make_template(
      "math", "sum", "Sum", {"a", "b"}, {"y"}, {ParamDef{.id = "signs", .type = "text", .default_text = "++"}},
      [](const Values&, const Config& text) {
        const auto signs = parse_signs(text.contains("signs") ? text.at("signs") : "");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          double y = 0;
          for (std::size_t i = 0; i < signs.size(); ++i)
            y += signs[i] * get_or(in, std::string(1, static_cast<char>('a' + i)));
          return Values{{"y", y}};
        });
      },
      "Adds a and b.", {.compact = true, .glyph = "plus", .icon = plus_icon()});
  sum.def.ports_from = "signs";
  add_pointwise(registry, std::move(sum));
}

}  // namespace paperboat::toolkit::math
