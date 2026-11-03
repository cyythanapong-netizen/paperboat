#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/expression.hpp"
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>

#include "paperboat/toolkit/control/simulator.hpp"
#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::WithinAbs;

namespace {

std::unique_ptr<Block> make(const std::string& name, const Values& params) {
  const Template* t = builtins().find(name);
  REQUIRE(t != nullptr);
  return t->make(params, t->config);
}

double out(Block& b, double t, const Values& in = {}, const std::string& port = "y", double dt = 0.01) {
  return get_or(b.step(Context{t, dt}, in), port);
}

}  // namespace

TEST_CASE("templates are named by group and typed by their last segment") {
  for (const char* name : {"signal/sine", "signal/step", "signal/ramp", "signal/square", "math/gain",
                           "math/sum", "math/threshold", "math/saturation", "control/pid",
                           "control/integrator", "control/lag"}) {
    const Template* t = builtins().find(name);
    REQUIRE(t != nullptr);
    CHECK(std::string(name).ends_with("/" + t->def.type));
  }
}

TEST_CASE("waves") {
  auto sine = make("signal/sine", {{"amplitude", 2}, {"frequency", 0.5}, {"phase", 0}, {"offset", 1}});
  CHECK_THAT(out(*sine, 0.5), WithinAbs(1 + 2 * std::sin(std::numbers::pi / 2), 1e-12));  // peak

  auto step = make("signal/step", {{"at", 1}, {"value", 3}});
  CHECK(out(*step, 0.99) == 0.0);
  CHECK(out(*step, 1.0) == 3.0);

  auto ramp = make("signal/ramp", {{"start", 1}, {"slope", 2}});
  CHECK(out(*ramp, 0.5) == 0.0);
  CHECK_THAT(out(*ramp, 2.0), WithinAbs(2.0, 1e-12));

  auto square = make("signal/square", {{"amplitude", 1.5}, {"frequency", 1}});
  CHECK(out(*square, 0.25) == 1.5);
  CHECK(out(*square, 0.75) == -1.5);
}

TEST_CASE("math") {
  auto gain = make("math/gain", {{"k", -2}});
  CHECK(out(*gain, 0, {{"u", 3}}) == -6.0);
  CHECK(out(*gain, 0) == 0.0);  // unconnected input reads as 0

  auto sum = make("math/sum", {});
  CHECK(out(*sum, 0, {{"a", 1.5}, {"b", 2}}) == 3.5);

  auto threshold = make("math/threshold", {{"level", 0.3}});
  CHECK(out(*threshold, 0, {{"u", 0.31}}) == 1.0);
  CHECK(out(*threshold, 0, {{"u", 0.3}}) == 0.0);

  auto sat = make("math/saturation", {{"min", -1}, {"max", 2}});
  CHECK(out(*sat, 0, {{"u", 5}}) == 2.0);
  CHECK(out(*sat, 0, {{"u", -5}}) == -1.0);
  CHECK(out(*sat, 0, {{"u", 0.5}}) == 0.5);
}

TEST_CASE("pid") {
  const double dt = 0.1;
  auto u = [&](Block& b, double t, double e) { return out(b, t, {{"e", e}}, "u", dt); };

  SECTION("proportional only") {
    auto p = make("control/pid", {{"kp", 4}, {"ki", 0}, {"kd", 0}});
    CHECK(u(*p, 0, 0.5) == 2.0);
  }
  SECTION("integral accumulates e·dt") {
    auto i = make("control/pid", {{"kp", 0}, {"ki", 1}, {"kd", 0}});
    u(*i, 0.0, 1);
    u(*i, 0.1, 1);
    CHECK_THAT(u(*i, 0.2, 1), WithinAbs(0.3, 1e-12));
  }
  SECTION("derivative is zero on the first step, then a backward difference") {
    auto d = make("control/pid", {{"kp", 0}, {"ki", 0}, {"kd", 1}});
    CHECK(u(*d, 0.0, 1) == 0.0);
    CHECK_THAT(u(*d, 0.1, 1.5), WithinAbs(5.0, 1e-12));
  }
  SECTION("instances don't share state") {
    auto a = make("control/pid", {{"kp", 0}, {"ki", 1}, {"kd", 0}});
    auto b = make("control/pid", {{"kp", 0}, {"ki", 1}, {"kd", 0}});
    u(*a, 0, 10);
    CHECK_THAT(u(*b, 0, 1), WithinAbs(0.1, 1e-12));
  }
}

TEST_CASE("integrator and lag") {
  const double dt = 0.1;
  auto integ = make("control/integrator", {{"initial", 1}});
  double y = 0;
  for (int k = 0; k <= 10; ++k) y = out(*integ, k * dt, {{"u", 2}}, "y", dt);
  CHECK_THAT(y, WithinAbs(1 + 2 * 1.0, 1e-9));  // output lags its input by one step

  auto lag = make("control/lag", {{"tau", 0.5}});
  double z = 0;
  for (int k = 0; k < 100; ++k) z = out(*lag, k * dt, {{"u", 3}}, "y", dt);
  CHECK_THAT(z, WithinAbs(3.0, 1e-6));

  auto none = make("control/lag", {{"tau", 0}});
  CHECK(out(*none, 0, {{"u", 4}}) == 4.0);
}

TEST_CASE("rk4_step matches exp(-t)") {
  using control::State;
  State<1> x{1.0};
  const double dt = 0.01;
  for (int k = 0; k < 100; ++k) {
    x = control::rk4_step(x, k * dt, dt, [](double, const State<1>& s) { return State<1>{-s[0]}; });
  }
  CHECK_THAT(x[0], WithinAbs(std::exp(-1.0), 1e-8));
}

TEST_CASE("base/constant outputs its value") {
  const auto* t = paperboat::toolkit::builtins().find("base/constant");
  REQUIRE(t);
  paperboat::toolkit::Context ctx{.t = 3.0, .dt = 0.01, .keys_down = nullptr};
  CHECK(t->make({{"value", 2.5}}, {})->step(ctx, {}).at("y") == 2.5);
}

TEST_CASE("base values: number, text and boolean") {
  const auto& reg = paperboat::toolkit::builtins();
  CHECK(reg.find("base/constant")->def.label == "Number");
  const auto* text = reg.find("base/text");
  REQUIRE(text != nullptr);
  CHECK(text->def.params.at(0).type == "text");
  auto hello = text->make({}, {{"value", "fold"}});
  CHECK(get_text(hello->step({}, {}), "y") == "fold");
  const auto* boolean = reg.find("base/boolean");
  REQUIRE(boolean != nullptr);
  CHECK(boolean->def.params.at(0).is_number());
  CHECK(get_or(boolean->make({{"value", 0}}, {})->step({}, {}), "y") == 0.0);
  CHECK(get_or(boolean->make({{"value", 1}}, {})->step({}, {}), "y") == 1.0);
}

TEST_CASE("ease shapes a progress (a definition on signal/generator)") {
  const auto* ease = paperboat::toolkit::builtins().find("signal/ease");
  REQUIRE(ease != nullptr);
  // A catalog entry makes it: the shape (an enum) by name, the block gets its index too.
  const auto catalog = load_descriptor("domain = \"e\"\nimport = [\"signal\"]\n");
  const auto* entry = catalog.find("ease");
  REQUIRE(entry->def.params.at(0).type == "enum");
  auto at = [&](const char* shape, double u) {
    return get_or(entry->make({}, {{"shape", shape}})->step({}, {{"t", u}}), "y");
  };
  CHECK(at("in-out", 0.5) == 0.5);  // smoothstep is symmetric
  CHECK(at("in-out", 0.25) < 0.25);
  CHECK(at("in", 0.5) == 0.25);
  CHECK(at("out", 0.5) == 0.75);
  CHECK(at("linear", 0.3) == 0.3);
  CHECK(at("in-out", 2.0) == 1.0);  // clamped
  CHECK_THROWS_WITH(entry->make({}, {{"shape", "bouncy"}}), Catch::Matchers::ContainsSubstring("not one of: in-out, in, out, linear"));
}

TEST_CASE("signal blocks are TOML definitions on a C++ parent; its f is an expression") {
  const auto& reg = paperboat::toolkit::builtins();
  const auto* parent = reg.find("signal/generator");
  REQUIRE(parent != nullptr);
  CHECK(parent->abstract);
  const auto* sine = reg.find("signal/sine");
  REQUIRE(sine != nullptr);
  CHECK(sine->config.at("f").find("sin(") != std::string::npos);
  CHECK(sine->def.inputs.at(0).id == "t");  // f(t): t is an input, the clock when unwired
  auto s = sine->make({{"amplitude", 2}, {"frequency", 0.25}, {"phase", 0}, {"offset", 1}}, sine->config);
  CHECK_THAT(get_or(s->step(Context{1.0, 0.01}, {}), "y"), WithinAbs(3.0, 1e-12));  // the clock
  CHECK_THAT(get_or(s->step(Context{1.0, 0.01}, {{"t", 3.0}}), "y"), WithinAbs(-1.0, 1e-12));  // its input
  const auto* fn = reg.find("signal/function");
  REQUIRE(fn != nullptr);
  CHECK(fn->def.params.at(0).type == "text");  // f(t): typed on the block
  auto f = fn->make({{"a", 2}, {"b", 1}, {"c", 0}}, {{"f", "a * t^2 + c"}});
  CHECK(get_or(f->step(Context{3.0, 0.01}, {}), "y") == 18.0);
  CHECK_THROWS_WITH(fn->make({{"a", 1}}, {{"f", "a * sin(tt)"}}), Catch::Matchers::ContainsSubstring("unknown name \"tt\""));
}

TEST_CASE("expressions: precedence, functions, errors") {
  using paperboat::toolkit::Expression;
  const std::vector<double> none;
  auto eval = [&](const char* text) { return Expression(text, {})(none); };
  CHECK(eval("1 + 2 * 3") == 7);
  CHECK(eval("-2^2") == -4);
  CHECK(eval("2^3^2") == 512);
  CHECK(eval("(1 + 2) * 3") == 9);
  CHECK(eval("1 < 2 && 3 >= 3") == 1);
  CHECK(eval("0 ? 1 : 2 > 1 ? 5 : 6") == 5);
  CHECK(eval("clamp(5, 0, 1) + min(2, 3) + mod(7, 4)") == 6);
  CHECK(eval("!0 + !(2 != 2)") == 2);
  CHECK_THAT(eval("sin(pi / 2) + log(e)"), WithinAbs(2.0, 1e-12));
  const Expression x("k * x + 1", {"x", "k"});
  const std::vector<double> xk{3, 2};
  CHECK(x(xk) == 7);
  CHECK_THROWS_WITH(Expression("1 +", {}), Catch::Matchers::ContainsSubstring("at 4"));
  CHECK_THROWS_WITH(Expression("sinn(1)", {}), Catch::Matchers::ContainsSubstring("unknown function \"sinn\""));
  CHECK_THROWS_WITH(Expression("max(1)", {}), Catch::Matchers::ContainsSubstring("takes 2 arguments"));
  CHECK_THROWS_WITH(Expression("  ", {}), Catch::Matchers::ContainsSubstring("empty"));
}
