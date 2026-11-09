// Solvers, Model and the scene builders: the tools domain code writes with.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
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
  m.restart();  // the equations may have changed: the solver starts afresh
  CHECK_THAT(get_or(m.read(), "x"), WithinAbs(std::cos(2.0), 1e-8));
  // A force held through the step: equilibrium at force / k.
  Mass pushed("rk45");
  for (int k = 0; k < 2000; ++k) pushed.step(ctx, {{"force", 2.0}});
  CHECK(std::abs(get_or(pushed.read(), "x") - 0.5) <= 0.5 + 1e-9);  // swings about 0.5, never past 0 or 1
  CHECK_THROWS(Mass("magic"));
}

namespace {

bool has(std::string_view solver) { return std::ranges::find(solve::names(), solver) != solve::names().end(); }

// Robertson's chemical kinetics (Hairer & Wanner II, ROBER): the classic stiff test, its rate
// constants 0.04 to 3e7 apart.
State robertson(double, const State& y) {
  return {-0.04 * y[0] + 1e4 * y[1] * y[2], 0.04 * y[0] - 1e4 * y[1] * y[2] - 3e7 * y[1] * y[1], 3e7 * y[1] * y[1]};
}

// The largest error of x against ref, relative to each component.
double rel_error(const State& x, const State& ref) {
  double worst = 0;
  for (std::size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(x[i] - ref[i]) / std::abs(ref[i]));
  return worst;
}

}  // namespace

TEST_CASE("cvode and ida are there, or say they need SUNDIALS (#217)") {
  if (!has("cvode")) {
    CHECK_THROWS_WITH(solve::make("cvode"), Catch::Matchers::ContainsSubstring("needs an engine built with SUNDIALS"));
    CHECK_THROWS_WITH(solve::make("ida"), Catch::Matchers::ContainsSubstring("needs an engine built with SUNDIALS"));
    State x = {1};
    CHECK_THROWS(solve::step("cvode", x, 0, 0.1, [](double, const State& s) { return State{-s[0]}; }));
  }
}

TEST_CASE("cvode and ida solve Robertson's stiff kinetics on the clock (#217)") {
  for (const char* name : {"cvode", "ida"}) {
    if (!has(name)) SKIP("the engine is built without SUNDIALS");
    INFO(name);
    // References: Radau IIA at rtol 1e-13; t = 40 and 1e11 as in Hairer & Wanner and the IVP test set (Mazzia).
    const std::vector<std::pair<double, State>> refs = {
        {0.4, {9.851721138609896e-01, 3.386395378974904e-05, 1.479402218522038e-02}},
        {4, {9.055186785842533e-01, 2.240475687560204e-05, 9.445891665887007e-02}},
        {40, {7.158270687193941e-01, 9.185534764557e-06, 2.841637457458293e-01}},
    };
    for (const double rtol : {1e-4, 1e-8}) {
      auto solver = solve::make(name, {.rtol = rtol, .atol = 1e-20});
      int calls = 0;
      auto f = [&](double t, const State& y) { ++calls; return robertson(t, y); };
      State y = {1, 0, 0};
      const double dt = 0.01;  // the clock: 4000 steps to t = 40
      std::size_t next = 0;
      for (int k = 0; k < 4000; ++k) {
        solver->step(y, k * dt, dt, f);
        const double t = (k + 1) * dt;
        if (next < refs.size() && std::abs(t - refs[next].first) < dt / 2) {
          const double err = rel_error(y, refs[next].second);
          INFO("rtol " << rtol << ", t = " << t << ": relative error " << err);
          CHECK(err < 200 * rtol);
          ++next;
        }
      }
      CHECK(next == refs.size());
      CHECK_THAT(y[0] + y[1] + y[2], WithinAbs(1.0, 1e-9));  // mass is conserved
      INFO("rtol " << rtol << ": " << calls << " evaluations");
      CHECK(calls < 60000);  // a few per clock step: the step grows to the clock's
    }

    // To t = 1e11 in decades of clock step, where an explicit method would need some 1e15 steps.
    auto solver = solve::make(name, {.rtol = 1e-8, .atol = 1e-22, .max_steps = 5000});
    State y = {1, 0, 0};
    double t = 0;
    for (double next = 1e-6; next <= 1e11 * (1 + 1e-9); next *= 10) {  // to 1e-6, 1e-5, …, 1e11
      solver->step(y, t, next - t, robertson);
      t = next;
    }
    const double err = rel_error(y, {2.083340149701255e-08, 8.333360770334713e-14, 9.999999791665050e-01});
    INFO("t = 1e11: y = " << y[0] << ", " << y[1] << ", " << y[2] << "; relative error " << err);
    CHECK(err < 1e-4);
  }
  // rk45 on the same clock steps is held to its stability limit: it gives up.
  auto rk45 = solve::make("rk45", {.rtol = 1e-8, .max_steps = 1000});
  State y = {1, 0, 0};
  CHECK_THROWS_WITH([&] { for (int k = 0; k < 40; ++k) rk45->step(y, k * 1.0, 1.0, robertson); }(),
                    Catch::Matchers::ContainsSubstring("rk45"));  // too stiff: out of max_steps, or not a number
}

TEST_CASE("cvode and ida on a stiff linear system with a known solution (#217)") {
  // x' = -1000 (x - cos t) - sin t: x = cos t + (x0 - 1) e^(-1000 t). At a 0.1 s clock rk4 diverges.
  auto f = [](double t, const State& x) { return State{-1000 * (x[0] - std::cos(t)) - std::sin(t)}; };
  for (const char* name : {"cvode", "ida"}) {
    if (!has(name)) SKIP("the engine is built without SUNDIALS");
    INFO(name);
    auto solver = solve::make(name, {.rtol = 1e-8, .atol = 1e-10});
    State x = {2};
    double worst = 0;
    for (int k = 0; k < 100; ++k) {
      solver->step(x, k * 0.1, 0.1, f);
      const double t = (k + 1) * 0.1;
      worst = std::max(worst, std::abs(x[0] - (std::cos(t) + std::exp(-1000 * t))));
    }
    INFO("largest error over 10 s: " << worst);
    CHECK(worst < 1e-6);
  }
  State x = {2};
  auto rk4 = solve::make("rk4");
  for (int k = 0; k < 100; ++k) rk4->step(x, k * 0.1, 0.1, f);
  CHECK_FALSE(std::abs(x[0] - std::cos(10.0)) < 1);  // off, or not a number at all
}

TEST_CASE("cvode and ida match rk4 on a smooth system (#217)") {
  State ref = {1, 0};
  auto rk4 = solve::make("rk4");
  for (int k = 0; k < 1000; ++k) rk4->step(ref, k * 0.01, 0.01, oscillator);  // t = 10
  for (const char* name : {"cvode", "ida"}) {
    if (!has(name)) SKIP("the engine is built without SUNDIALS");
    INFO(name);
    auto solver = solve::make(name, {.rtol = 1e-10, .atol = 1e-12});
    State x = {1, 0};
    double worst = 0;
    for (int k = 0; k < 1000; ++k) {
      solver->step(x, k * 0.01, 0.01, oscillator);
      worst = std::max(worst, std::abs(x[0] - std::cos((k + 1) * 0.01)));
    }
    INFO("largest error against cos t: " << worst << "; against rk4 at t = 10: " << std::abs(x[0] - ref[0]));
    CHECK(worst < 1e-7);
    CHECK_THAT(x[0], WithinAbs(ref[0], 1e-7));
    CHECK_THAT(x[1], WithinAbs(ref[1], 1e-7));
    // The default tolerances (rtol 1e-6) hold too, an order or two looser.
    State y = {1, 0};
    auto loose = solve::make(name);
    for (int k = 0; k < 1000; ++k) loose->step(y, k * 0.01, 0.01, oscillator);
    CHECK_THAT(y[0], WithinAbs(std::cos(10.0), 1e-4));
  }
}

TEST_CASE("cvode and ida start afresh when the state changed between steps (#217)") {
  for (const char* name : {"cvode", "ida"}) {
    if (!has(name)) SKIP("the engine is built without SUNDIALS");
    INFO(name);
    // x' = -x, put back to 1 before each step (a reset): each step is e^-dt from 1.
    auto solver = solve::make(name, {.rtol = 1e-10, .atol = 1e-12});
    auto decay = [](double, const State& x) { return State{-x[0]}; };
    for (int k = 0; k < 5; ++k) {
      State x = {1};
      solver->step(x, k * 0.5, 0.5, decay);
      CHECK_THAT(x[0], WithinAbs(std::exp(-0.5), 1e-8));
    }
    // A clock that jumps back (a new run on the same solver) starts afresh too.
    State x = {1};
    solver->step(x, 0, 1, decay);
    CHECK_THAT(x[0], WithinAbs(std::exp(-1.0), 1e-8));
    // A state of another size: a new integrator.
    State two = {1, 2};
    solver->step(two, 0, 1, [](double, const State& s) { return State{-s[0], -2 * s[1]}; });
    CHECK_THAT(two[1], WithinAbs(2 * std::exp(-2.0), 1e-8));
    // restart(): the equations changed (x' = +x now), it starts afresh and follows them.
    solver->restart();
    solver->step(two, 1, 1, [](double, const State& s) { return State{s[0], s[1]}; });
    CHECK_THAT(two[0], WithinAbs(std::exp(-1.0) * std::exp(1.0), 1e-8));
  }
}

TEST_CASE("cvode and ida say what's wrong (#217)") {
  for (const char* name : {"cvode", "ida"}) {
    if (!has(name)) SKIP("the engine is built without SUNDIALS");
    INFO(name);
    State x = {1, 2};
    CHECK_THROWS_WITH(solve::step(name, x, 0, 0.1, [](double, const State&) { return State{1}; }),
                      Catch::Matchers::ContainsSubstring("returned 1 values for a state of 2"));
    // The equations' own exception comes through as it was.
    CHECK_THROWS_WITH(solve::step(name, x, 0, 0.1, [](double, const State&) -> State { throw std::runtime_error("no gas"); }),
                      Catch::Matchers::Equals("no gas"));
    // Too few internal steps for the clock step.
    State y = {1, 0, 0};
    CHECK_THROWS_WITH(solve::make(name, {.rtol = 1e-10, .max_steps = 5})->step(y, 0, 40, robertson),
                      Catch::Matchers::ContainsSubstring("max_steps 5"));
    // A blow-up: x' = x^2 from 1 reaches infinity at t = 1.
    State z = {1};
    CHECK_THROWS_WITH(solve::step(name, z, 0, 2, [](double, const State& s) { return State{s[0] * s[0]}; }),
                      Catch::Matchers::StartsWith(std::string(name) + ": "));
    // An empty state has nothing to do.
    State none;
    solve::step(name, none, 0, 1, [](double, const State&) { return State{}; });
    CHECK(none.empty());
  }
  // The run's solver may be cvode: "auto" and a Model use it.
  if (has("cvode")) {
    solve::set_defaults({"cvode", {.rtol = 1e-9, .atol = 1e-12}});
    CHECK(solve::make()->name() == "cvode");
    Mass m("auto");
    Context ctx{.t = 0, .dt = 0.01};
    for (int k = 0; k < 100; ++k, ctx.t += ctx.dt) m.step(ctx, {});
    CHECK_THAT(get_or(m.read(), "x"), WithinAbs(std::cos(2.0), 1e-6));
    solve::set_defaults({});
  }
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
