// math/inverse: A⁻¹ (Eigen's partial-pivot LU).

#include <memory>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_inverse(Registry& registry) {
  Template t;
  t.name = "math/inverse";
  t.def.type = "inverse";
  t.def.label = "Inverse";
  t.def.inputs = {matrix_port("a")};
  t.def.outputs = {matrix_port("y")};
  t.def.description = "The inverse of a square, non-singular matrix (A⁻¹).";
  t.def.view = {.compact = true, .glyph = "A⁻¹"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table matrix = matrix_at(in, "a");
      require_rectangular(matrix, "inverse");
      if (rows(matrix) == 0 || rows(matrix) != columns(matrix))
        throw std::invalid_argument("inverse: expects a square matrix, got " + std::to_string(rows(matrix)) + " × " +
                                    std::to_string(columns(matrix)));
      const Eigen::MatrixXd a = to_eigen(matrix, "inverse");
      const Eigen::FullPivLU<Eigen::MatrixXd> lu(a);
      if (!lu.isInvertible()) throw std::invalid_argument("inverse: the matrix is singular");
      return Values{{"y", to_table(lu.inverse())}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
