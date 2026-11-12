// math/multiply: A·B.

#include <memory>
#include <stdexcept>
#include <string>

#include "common.hpp"
#include "math/matrix_shared.hpp"

namespace paperboat::toolkit::math {

void add_multiply(Registry& registry) {
  Template t;
  t.name = "math/multiply";
  t.def.type = "multiply";
  t.def.label = "Matrix multiply";
  t.def.inputs = {matrix_port("a"), matrix_port("b")};
  t.def.outputs = {matrix_port("y")};
  t.def.description = "The matrix product A·B; the inner dimensions must agree.";
  t.def.view = {.compact = true, .glyph = "A·B"};
  t.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      const Table a = matrix_at(in, "a");
      const Table b = matrix_at(in, "b");
      require_rectangular(a, "multiply: a");
      require_rectangular(b, "multiply: b");
      if (rows(a) == 0 || rows(b) == 0)
        throw std::invalid_argument("multiply: both inputs need a matrix (a is " + std::to_string(rows(a)) + " × " +
                                    std::to_string(columns(a)) + ", b is " + std::to_string(rows(b)) + " × " +
                                    std::to_string(columns(b)) + ")");
      if (columns(a) != rows(b))
        throw std::invalid_argument("multiply: a is " + std::to_string(rows(a)) + " × " + std::to_string(columns(a)) +
                                    " but b is " + std::to_string(rows(b)) + " × " + std::to_string(columns(b)) +
                                    ": a's columns must equal b's rows");
      return Values{{"y", to_table(to_eigen(a, "multiply: a") * to_eigen(b, "multiply: b"))}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
