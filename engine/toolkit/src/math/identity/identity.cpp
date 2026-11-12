// math/identity: the n × n identity matrix.

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_identity(Registry& registry) {
  Template t;
  t.name = "math/identity";
  t.def.type = "identity";
  t.def.label = "Identity";
  t.def.kind = Kind::Source;
  t.def.outputs = {matrix_port("y")};
  t.def.params = {{.id = "n", .default_value = 2.0}};
  t.def.description = "The n × n identity matrix: ones on the diagonal, zeros elsewhere.";
  t.def.view = {.compact = true, .glyph = "I", .value = "I{n}"};
  t.make = [](const Values& params, const Config&) {
    const double n = get_or(params, "n", 2.0);
    if (!std::isfinite(n) || n < 1 || n != std::floor(n) || n > 256)
      throw std::invalid_argument("identity: n must be a whole number from 1 to 256");
    const auto size = static_cast<std::size_t>(n);
    Table matrix(size, std::vector<double>(size, 0.0));
    for (std::size_t i = 0; i < size; ++i) matrix[i][i] = 1.0;
    return std::make_unique<Stateless>(
        [matrix = std::move(matrix)](const Context&, const Values&) { return Values{{"y", matrix}}; });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
