// math/solve: A \ B (MATLAB's backslash) via Eigen's LU.

#include <memory>
#include <stdexcept>
#include <string>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_solve(Registry& registry) {
  Template t;
  t.name = "math/solve";
  t.def.type = "solve";
  t.def.label = "Solve";
  t.def.inputs = {matrix_port("a"), matrix_port("b")};
  t.def.outputs = {matrix_port("x")};
  t.def.description = "Solves A·x = B for x (MATLAB's A \\ B); A must be square and non-singular.";
  t.def.view = {.compact = true, .glyph = "A\\B"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table a = matrix_at(in, "a");
      const Table b = matrix_at(in, "b");
      require_rectangular(a, "solve: a");
      require_rectangular(b, "solve: b");
      if (rows(a) == 0 || rows(a) != columns(a))
        throw std::invalid_argument("solve: a must be square, got " + std::to_string(rows(a)) + " × " +
                                    std::to_string(columns(a)));
      if (rows(b) != rows(a))
        throw std::invalid_argument("solve: b has " + std::to_string(rows(b)) + " rows, expected " +
                                    std::to_string(rows(a)));
      const Eigen::MatrixXd matrix = to_eigen(a, "solve: a");
      const Eigen::FullPivLU<Eigen::MatrixXd> lu(matrix);
      if (!lu.isInvertible()) throw std::invalid_argument("solve: a is singular");
      return Values{{"x", to_table(lu.solve(to_eigen(b, "solve: b")))}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
