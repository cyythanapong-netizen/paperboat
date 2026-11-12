// math/size: a matrix's rows and columns.

#include <memory>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_size(Registry& registry) {
  Template t;
  t.name = "math/size";
  t.def.type = "size";
  t.def.label = "Size";
  t.def.inputs = {matrix_port("a")};
  t.def.outputs = {{"rows"}, {"cols"}};
  t.def.description = "The shape of a matrix: how many rows and columns it has.";
  t.def.view = {.compact = true, .glyph = "size", .value = "{rows}×{cols}"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table matrix = matrix_at(in, "a");
      require_rectangular(matrix, "size");
      return Values{{"rows", static_cast<double>(rows(matrix))}, {"cols", static_cast<double>(columns(matrix))}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
