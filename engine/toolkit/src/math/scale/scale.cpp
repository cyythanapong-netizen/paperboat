// math/scale: k·A.

#include <memory>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_scale(Registry& registry) {
  Template t;
  t.name = "math/scale";
  t.def.type = "scale";
  t.def.label = "Scale matrix";
  t.def.inputs = {matrix_port("a")};
  t.def.outputs = {matrix_port("y")};
  t.def.params = {{.id = "k", .default_value = 2.0}};
  t.def.description = "Multiplies every element of a matrix by k.";
  t.def.view = {.compact = true, .glyph = "k·A", .value = "{k}"};
  t.make = [](const Values& params, const Config&) {
    const double k = get_or(params, "k", 2.0);
    return std::make_unique<Stateless>([k](const Context&, const Values& in) {
      const Table matrix = matrix_at(in, "a");
      require_rectangular(matrix, "scale");
      Table out = matrix;
      for (auto& row : out)
        for (double& value : row) value *= k;
      return Values{{"y", std::move(out)}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
