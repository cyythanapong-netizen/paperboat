// math/transpose: Aᵀ.

#include <memory>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_transpose(Registry& registry) {
  Template t;
  t.name = "math/transpose";
  t.def.type = "transpose";
  t.def.label = "Transpose";
  t.def.inputs = {matrix_port("a")};
  t.def.outputs = {matrix_port("y")};
  t.def.description = "The transpose of a matrix: rows become columns.";
  t.def.view = {.compact = true, .glyph = "Aᵀ"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table matrix = matrix_at(in, "a");
      require_rectangular(matrix, "transpose");
      Table out(columns(matrix), std::vector<double>(rows(matrix)));
      for (std::size_t i = 0; i < rows(matrix); ++i)
        for (std::size_t j = 0; j < columns(matrix); ++j) out[j][i] = matrix[i][j];
      return Values{{"y", std::move(out)}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
