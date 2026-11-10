#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <memory>
#include <string>

#include "paperboat/toolkit/optimization.hpp"
#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

const Registry& registry() {
  static const Registry with_optimization = [] {
    Registry r = builtins();
    optimization::register_optimization(r);
    return r;
  }();
  return with_optimization;
}

std::unique_ptr<Step> solve(const std::string& name, const std::string& file) {
  const Template* t = registry().find(name);
  REQUIRE(t != nullptr);
  REQUIRE(t->make_step);
  return t->make_step({{"eps_abs", 1e-9}, {"eps_rel", 1e-9}, {"max_iter", 10000.0}}, Config{{"file", file}});
}

Values run(Step& step) {
  step.enter(Context{}, {});
  CHECK(step.tick(Context{}, {}) == "out");
  return step.outputs();
}

}  // namespace

TEST_CASE("optimization/qp solves a quadratic program (OSQP)") {
  auto step = solve("optimization/qp", std::string(PAPERBOAT_TEST_DATA) + "/qp.json");
  const Values out = run(*step);
  const Vector* x = get_vector(out, "x");
  REQUIRE(x != nullptr);
  REQUIRE(x->size() == 2);
  CHECK_THAT((*x)[0], WithinAbs(1.0, 1e-4));
  CHECK_THAT((*x)[1], WithinAbs(0.0, 1e-4));
  CHECK_THAT(get_or(out, "objective"), WithinAbs(-1.5, 1e-4));
  CHECK_THAT(get_text(out, "status"), ContainsSubstring("solved"));
}

TEST_CASE("optimization/lp solves a linear program (OSQP)") {
  auto step = solve("optimization/lp", std::string(PAPERBOAT_TEST_DATA) + "/lp.json");
  const Values out = run(*step);
  const Vector* x = get_vector(out, "x");
  REQUIRE(x != nullptr);
  REQUIRE(x->size() == 2);
  CHECK_THAT((*x)[0], WithinAbs(0.6, 1e-4));
  CHECK_THAT((*x)[1], WithinAbs(0.4, 1e-4));
  CHECK_THAT(get_or(out, "objective"), WithinAbs(-1.6, 1e-4));
}

TEST_CASE("optimization blocks name the file they cannot read") {
  auto step = solve("optimization/qp", "/nonexistent/paperboat-qp.json");
  CHECK_THROWS_WITH(step->enter(Context{}, {}), ContainsSubstring("cannot open"));
}
