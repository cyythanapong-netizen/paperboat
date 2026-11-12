// math/determinant: det(A) (Eigen's LU).

#include <memory>
#include <stdexcept>
#include <string>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_determinant(Registry& registry) {
  Template t;
  t.name = "math/determinant";
  t.def.type = "determinant";
  t.def.label = "Determinant";
  t.def.inputs = {matrix_port("a")};
  t.def.outputs = {{"det"}};
  t.def.description = "The determinant of a square matrix.";
  t.def.view = {.compact = true, .glyph = "det", .value = "det(A)"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table matrix = matrix_at(in, "a");
      require_rectangular(matrix, "determinant");
      if (rows(matrix) == 0 || rows(matrix) != columns(matrix))
        throw std::invalid_argument("determinant: expects a square matrix, got " + std::to_string(rows(matrix)) + " × " +
                                    std::to_string(columns(matrix)));
      return Values{{"det", to_eigen(matrix, "determinant").determinant()}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
