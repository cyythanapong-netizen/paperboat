#pragma once

// Expressions over named numbers, compiled once and evaluated many times: what lets a
// block be defined in TOML on a C++ parent (signal/generator's f(t), say).
//
//   numbers 1, 2.5e-3 · names (the variables, and pi, e) · ( ) · unary - + !
//   ^ (power, right to left) · * / % · + - · < <= > >= == != (1 or 0) · && || · c ? a : b
//   functions: sin cos tan asin acos atan atan2 sinh cosh tanh exp log log10 sqrt abs
//              floor ceil round sign frac min max clamp(x, lo, hi) mod(a, b) pow(a, b)

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace paperboat::toolkit {

struct ExpressionError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Expression {
 public:
  // Compile `text` over `variables` (their values are passed in this order). Throws
  // ExpressionError, saying where: `at 7: unknown name "frequncy" (known: t, amplitude…)`.
  Expression(std::string_view text, std::vector<std::string> variables);

  double operator()(std::span<const double> values) const;
  // True for a name an expression gives its own meaning: pi, e, a function (sin, clamp…).
  static bool is_builtin(std::string_view name);
  const std::vector<std::string>& variables() const { return variables_; }
  const std::string& text() const { return text_; }

  struct Node;

 private:
  std::string text_;
  std::vector<std::string> variables_;
  std::shared_ptr<const Node> root_;
};

}  // namespace paperboat::toolkit
