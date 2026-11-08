// Solvers, Model and the scene builders: the tools domain code writes with.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

#include "paperboat/toolkit/model.hpp"
#include "paperboat/toolkit/scene.hpp"
#include "paperboat/toolkit/solve.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using solve::State;

namespace {

// x'' = -x from (1, 0): x = cos t.
State oscillator(double, const State& x) { return {x[1], -x[0]}; }

// The error at t = 1 with `n` steps.
double error_with(const std::string& name, int n) {
  auto solver = solve::make(name);
  State x = {1, 0};
  const double dt = 1.0 / n;
  for (int k = 0; k < n; ++k) solver->step(x, k * dt, dt, oscillator);
  return std::abs(x[0] - std::cos(1.0));
}

}  // namespace

TEST_CASE("each fixed-step solver converges at its order") {
  // Halving the step divides the error by 2^order.
  for (const auto& [name, order] : {std::pair{"euler", 1}, {"rk2", 2}, {"rk4", 4}}) {
    const double coarse = error_with(name, 50), fine = error_with(name, 100);
    const double measured = std::log2(coarse / fine);
    INFO(name << ": measured order " << measured);
    CHECK_THAT(measured, WithinAbs(order, 0.15));
  }
}

TEST_CASE("rk45 holds its tolerance, sub-stepping inside the clock step") {
  // Van der Pol with mu = 5: stiff-ish, slow then sharp. A reference from rk4 at a tiny step.
  const double mu = 5;
  auto vdp = [mu](double, const State& x) { return State{x[1], mu * (1 - x[0] * x[0]) * x[1] - x[0]}; };
  State ref = {2, 0};
  auto rk4 = solve::make("rk4");
  for (int k = 0; k < 200000; ++k) rk4->step(ref, k * 1e-4, 1e-4, vdp);  // t = 20

  for (const double rtol : {1e-4, 1e-7}) {
    auto rk45 = solve::make("rk45", {.rtol = rtol, .atol = rtol * 1e-3, .max_steps = 100000});
    State x = {2, 0};
    for (int k = 0; k < 200; ++k) rk45->step(x, k * 0.1, 0.1, vdp);  // clock steps of 0.1 s
    INFO("rtol " << rtol << ": x = " << x[0] << ", reference " << ref[0]);
    CHECK_THAT(x[0], WithinAbs(ref[0], 2000 * rtol));  // a global error some hundreds of rtol
  }
  // At a clock step where rk4 would blow up, rk45 sub-steps and stays right.
  State x = {2, 0}, y = {2, 0};
  auto big = solve::make("rk4");
  auto adaptive = solve::make("rk45");
  for (int k = 0; k < 20; ++k) {
    big->step(x, k * 1.0, 1.0, vdp);
    adaptive->step(y, k * 1.0, 1.0, vdp);
  }
  CHECK_THAT(y[0], WithinAbs(ref[0], 1e-3));
  CHECK_FALSE(std::abs(x[0] - ref[0]) < 0.1);  // off, or not a number at all
}

TEST_CASE("discrete solves a difference equation") {
  // x[k+1] = 0.5 x[k] + 1 → 2.
  State x = {0};
  auto d = solve::make("discrete");
  for (int k = 0; k < 60; ++k) d->step(x, k, 1, [](double, const State& s) { return State{0.5 * s[0] + 1}; });
  CHECK_THAT(x[0], WithinAbs(2.0, 1e-12));
}

TEST_CASE("solvers say what's wrong") {
  CHECK_THROWS_WITH(solve::make("ode23"), Catch::Matchers::ContainsSubstring("rk4, rk45, rk2, euler, discrete"));
  State x = {1, 2};
  CHECK_THROWS_WITH(solve::step("rk4", x, 0, 0.1, [](double, const State&) { return State{1}; }),
                    Catch::Matchers::ContainsSubstring("returned 1 values for a state of 2"));
  const ParamDef p = solve::param();
  CHECK(p.type == "enum");
  CHECK(p.default_text == "auto");  // the run's solver (Run menu), unless one is picked
  CHECK(p.options.size() == solve::names().size() + 1);
  CHECK_THROWS_WITH(solve::make("rk4", {.sub_steps = 0}), Catch::Matchers::ContainsSubstring("sub_steps"));
  CHECK_THROWS_WITH(solve::make("rk45", {.min_step = 0.1, .max_step = 0.01}), Catch::Matchers::ContainsSubstring("min_step"));
  CHECK_THROWS_WITH(solve::make("rk45", {.rtol = 0}), Catch::Matchers::ContainsSubstring("rtol"));
}

TEST_CASE("solver options: sub-steps, max and min step (#196)") {
  // rk4 with 10 sub-steps of a 0.1 s clock step is rk4 at 0.01 s.
  State a = {1, 0}, b = {1, 0};
  auto sub = solve::make("rk4", {.sub_steps = 10});
  auto fine = solve::make("rk4");
  for (int k = 0; k < 10; ++k) sub->step(a, k * 0.1, 0.1, oscillator);
  for (int k = 0; k < 100; ++k) fine->step(b, k * 0.01, 0.01, oscillator);
  CHECK_THAT(a[0], WithinAbs(b[0], 1e-14));

  // rk45 never takes a sub-step longer than max_step: 7 evaluations a sub-step, 1 s / 0.01 s of them.
  int calls = 0;
  auto counted = [&](double t, const State& x) { ++calls; return oscillator(t, x); };
  State x = {1, 0};
  solve::make("rk45", {.max_step = 0.01})->step(x, 0, 1.0, counted);
  CHECK(calls >= 100 * 6);
  CHECK_THAT(x[0], WithinAbs(std::cos(1.0), 1e-8));

  // ... and gives up, saying so, when the error control wants a step below min_step.
  const double mu = 1000;  // stiff Van der Pol
  auto vdp = [mu](double, const State& s) { return State{s[1], mu * (1 - s[0] * s[0]) * s[1] - s[0]}; };
  State y = {2, 0};
  auto strict = solve::make("rk45", {.rtol = 1e-10, .min_step = 1e-3});
  CHECK_THROWS_WITH([&] { for (int k = 0; k < 100; ++k) strict->step(y, k * 1.0, 1.0, vdp); }(),
                    Catch::Matchers::ContainsSubstring("min_step"));
}

TEST_CASE("solver auto is the run's solver and options") {
  CHECK(solve::make()->name() == "rk4");  // no run: rk4
  solve::set_defaults({"euler", {.sub_steps = 10}});
  CHECK(solve::make("auto")->name() == "euler");
  CHECK(solve::make("")->name() == "euler");
  CHECK(solve::make("rk2")->name() == "rk2");  // a solver picked by name stays
  // the run's options too: euler in 10 sub-steps of 0.01 s is euler at 0.001 s
  State a = {1, 0}, b = {1, 0};
  solve::make()->step(a, 0, 0.01, oscillator);
  auto e = solve::make("euler", {});
  for (int k = 0; k < 10; ++k) e->step(b, k * 0.001, 0.001, oscillator);
  CHECK_THAT(a[0], WithinAbs(b[0], 1e-15));
  CHECK_THROWS(solve::set_defaults({"magic", {}}));
  solve::set_defaults({});
  CHECK(solve::make()->name() == "rk4");
}

TEST_CASE("fixed-size array solver overloads step correctly") {
  // x'' = -x from (1, 0) using std::array<double, 2>
  std::array<double, 2> x = {1.0, 0.0};
  auto rk4 = solve::make("rk4");
  const double dt = 0.01;
  for (int k = 0; k < 100; ++k) {
    rk4->step<2>(x, k * dt, dt, [](double, const std::array<double, 2>& s) -> std::array<double, 2> {
      return {s[1], -s[0]};
    });
  }
  CHECK_THAT(x[0], WithinAbs(std::cos(1.0), 1e-7));

  // Test free template function solve::step<2>
  std::array<double, 2> y = {1.0, 0.0};
  for (int k = 0; k < 100; ++k) {
    solve::step<2>("rk4", y, k * dt, dt, [](double, const std::array<double, 2>& s) -> std::array<double, 2> {
      return {s[1], -s[0]};
    });
  }
  CHECK_THAT(y[0], WithinAbs(std::cos(1.0), 1e-7));
}

namespace {

// A mass on a spring, as a domain would write it.
class Mass final : public Model {
 public:
  explicit Mass(std::string_view solver) : Model({1.0, 0.0}, solver) {}
  State derivatives(double, const State& x, const Values& in) const override {
    return {x[1], get_or(in, "force") - 4 * x[0]};
  }
  Values read() const override { return {{"x", x()[0]}}; }
  std::vector<Entity> entities(const std::string&, double) const override { return {}; }
};

}  // namespace

TEST_CASE("a Model steps itself with its solver") {
  Mass m("rk4");
  Context ctx{.t = 0, .dt = 0.01};
  for (int k = 0; k < 100; ++k, ctx.t += ctx.dt) m.step(ctx, {});
  CHECK_THAT(get_or(m.read(), "x"), WithinAbs(std::cos(2.0), 1e-8));  // ω = 2, t = 1
  // A force held through the step: equilibrium at force / k.
  Mass pushed("rk45");
  for (int k = 0; k < 2000; ++k) pushed.step(ctx, {{"force", 2.0}});
  CHECK(std::abs(get_or(pushed.read(), "x") - 0.5) <= 0.5 + 1e-9);  // swings about 0.5, never past 0 or 1
  CHECK_THROWS(Mass("magic"));
}

TEST_CASE("scene builders make protocol entities") {
  const Entity r = scene::rect("cart", 1, 2, 0.6, 0.3, "#123456", 0.5);
  CHECK(r.shape == "box");
  CHECK(r.pos == scene::Vec{1, 2, 0});
  CHECK(r.size[0] == 0.6);
  CHECK(r.rot[2] == 0.5);

  const Entity c = scene::circle("bob", 0, 1, 0.1);
  CHECK(c.shape == "sphere");
  CHECK(c.size[0] == 0.1);

  // A rod from a to b: centred between them, as long as they are apart, along their line.
  const Entity rod = scene::rod("pole", {0, 0, 0}, {0, 2, 0}, 0.05);
  CHECK(rod.pos == scene::Vec{0, 1, 0});
  CHECK_THAT(rod.size[0], WithinAbs(2, 1e-12));
  CHECK_THAT(rod.rot[2], WithinAbs(std::numbers::pi / 2, 1e-12));
  const Entity slant = scene::rod("slant", {0, 0, 0}, {-1, -1, 0}, 0.05);
  // Box along x, turned about z then y: its end is at b.
  const double up = slant.rot[2], round = slant.rot[1], half = slant.size[0] / 2;
  const double ex = half * std::cos(up) * std::cos(round), ey = half * std::sin(up);
  CHECK_THAT(slant.pos[0] - ex, WithinAbs(0, 1e-12));  // one end at a…
  CHECK_THAT(slant.pos[1] - ey, WithinAbs(0, 1e-12));
  CHECK_THAT(slant.pos[0] + ex, WithinAbs(-1, 1e-12));  // …the other at b
  CHECK_THAT(slant.pos[1] + ey, WithinAbs(-1, 1e-12));

  const auto coil = scene::spring("s", {0, 0, 0}, {2, 0, 0}, 4, 0.2);
  CHECK(coil.size() == 9);  // 2·4 + 2 points, 9 lines
  CHECK(coil.front().pos == scene::Vec{0, 0, 0});
  CHECK(coil.back().to == scene::Vec{2, 0, 0});
  CHECK(coil[3].id == "s/3");
  CHECK(scene::spring("s", {0, 0, 0}, {5, 0, 0}, 4).size() == coil.size());  // same lines at any length

  const Entity l = scene::line("l", {0, 0, 0}, {1, 1, 0}, "#f00");
  CHECK(l.to == scene::Vec{1, 1, 0});
}
