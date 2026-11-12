// math/matrix_constant: a matrix set by its text, MATLAB style ("[1 2; 3 4]").

#include <memory>
#include <string>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_matrix_constant(Registry& registry) {
  Template t;
  t.name = "math/matrix_constant";
  t.def.type = "matrix_constant";
  t.def.label = "Matrix";
  t.def.kind = Kind::Source;
  t.def.outputs = {matrix_port("y")};
  t.def.params = {{.id = "values", .type = "text", .default_text = "1 0; 0 1"}};
  t.def.description = "A matrix from its rows: \"1 2; 3 4\" (commas, newlines and brackets are welcome).";
  t.def.view = {.compact = true, .glyph = "[M]", .value = "{values}"};
  t.make = [](const Values&, const Config& text) {
    const Table matrix = parse_matrix(text.contains("values") ? text.at("values") : "1 0; 0 1", "values");
    return std::make_unique<Stateless>(
        [matrix](const Context&, const Values&) { return Values{{"y", matrix}}; });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
