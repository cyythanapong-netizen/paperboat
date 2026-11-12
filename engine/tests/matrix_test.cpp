// The math package's matrix blocks: a matrix is a rectangular Table on a signal port.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <string>

#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

std::unique_ptr<Block> make(const std::string& name, const Values& params = {}, const Config& text = {}) {
  const Template* t = builtins().find(name);
  REQUIRE(t != nullptr);
  Values with_defaults = params;
  for (const auto& p : t->def.params)
    if (p.is_number() && !with_defaults.contains(p.id)) with_defaults[p.id] = p.default_value;
  Config all_text = t->config;
  all_text.insert(text.begin(), text.end());
  return t->make(with_defaults, all_text);
}

Table matrix(const std::string& text) {
  auto constant = make("math/matrix_constant", {}, {{"values", text}});
  return std::get<Table>(constant->step(Context{}, {}).at("y"));
}

void check_matrix(const Table& actual, const Table& expected) {
  REQUIRE(actual.size() == expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    REQUIRE(actual[i].size() == expected[i].size());
    for (std::size_t j = 0; j < actual[i].size(); ++j) CHECK_THAT(actual[i][j], WithinAbs(expected[i][j], 1e-12));
  }
}

}  // namespace

TEST_CASE("math matrix blocks: constant, transpose, determinant, inverse, multiply") {
  const Table a = matrix("[1 2; 3 4]");
  CHECK(a == Table{{1, 2}, {3, 4}});
  CHECK(matrix("1, 2\n3, 4") == a);
  CHECK(matrix("[1 2 3]") == Table{{1, 2, 3}});

  CHECK(std::get<Table>(make("math/transpose")->step(Context{}, {{"a", a}}).at("y")) == Table{{1, 3}, {2, 4}});
  CHECK_THAT(get_or(make("math/determinant")->step(Context{}, {{"a", a}}), "det"), WithinAbs(-2.0, 1e-12));

  const Table inverse = std::get<Table>(make("math/inverse")->step(Context{}, {{"a", a}}).at("y"));
  check_matrix(inverse, {{-2.0, 1.0}, {1.5, -0.5}});

  const Table b = matrix("[5 6; 7 8]");
  const Table product = std::get<Table>(make("math/multiply")->step(Context{}, {{"a", a}, {"b", b}}).at("y"));
  check_matrix(product, {{19, 22}, {43, 50}});

  const Table rhs = matrix("[5; 6]");
  const Table solved = std::get<Table>(make("math/solve")->step(Context{}, {{"a", a}, {"b", rhs}}).at("x"));
  check_matrix(solved, {{-4}, {4.5}});

  const Table identity = std::get<Table>(make("math/identity", {{"n", 3}})->step(Context{}, {}).at("y"));
  check_matrix(identity, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});

  const Values size = make("math/size")->step(Context{}, {{"a", a}});
  CHECK(get_or(size, "rows") == 2.0);
  CHECK(get_or(size, "cols") == 2.0);

  const Table scaled = std::get<Table>(make("math/scale", {{"k", -2}})->step(Context{}, {{"a", a}}).at("y"));
  check_matrix(scaled, {{-2, -4}, {-6, -8}});
}

TEST_CASE("matrix ports travel on signal wires; values must be rectangular tables") {
  const PortDef matrix = matrix_port("a");
  CHECK(matrix.type == "signal");
  CHECK(matrix.matrix);
  CHECK_FALSE(wire_mismatch(matrix, matrix_port("b")).has_value());
  CHECK_FALSE(wire_mismatch(matrix, {.id = "u"}).has_value());
  CHECK(wire_mismatch(matrix, points_port("p")).value() == "joins a signal port to a points port");
  CHECK_FALSE(vector_value_mismatch(matrix, Value{Table{{1, 2}, {3, 4}}}));
  CHECK(vector_value_mismatch(matrix, Value{Table{{1, 2}, {3}}}).value() ==
        "has row 1 of 1 columns, expected 2");
  CHECK(vector_value_mismatch(matrix, Value{Vector{1, 2}}).value() == "expects a matrix (rows of numbers)");
}

TEST_CASE("matrix block errors name what is wrong") {
  CHECK_THROWS_WITH(make("math/matrix_constant", {}, {{"values", "1 2; 3"}}),
                    ContainsSubstring("row 1 has 1 columns, expected 2"));
  CHECK_THROWS_WITH(make("math/matrix_constant", {}, {{"values", "1 x"}}),
                    ContainsSubstring("\"x\" is not a finite number"));
  CHECK_THROWS_WITH(make("math/inverse")->step(Context{}, {{"a", matrix("1 2; 3 4; 5 6")}}),
                    ContainsSubstring("expects a square matrix, got 3 × 2"));
  CHECK_THROWS_WITH(make("math/inverse")->step(Context{}, {{"a", matrix("1 2; 2 4")}}),
                    ContainsSubstring("the matrix is singular"));
  CHECK_THROWS_WITH(make("math/multiply")->step(Context{}, {{"a", matrix("1 2 3")}, {"b", matrix("1 2 3")}}),
                    ContainsSubstring("a's columns must equal b's rows"));
  CHECK_THROWS_WITH(make("math/identity", {{"n", 0}}), ContainsSubstring("n must be a whole number"));
}
