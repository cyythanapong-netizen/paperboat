// Dynamic blocks for the flow: an integrator, a first-order lag and a delay. Domains use
// the integrators in paperboat/toolkit/control/simulator.hpp directly.

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "common.hpp"
#include "paperboat/toolkit/control/simulator.hpp"
#include "paperboat/toolkit/solve.hpp"

namespace paperboat::toolkit::control {
namespace {

// Blocks without direct feedthrough: each outputs its state (outputs()), then takes the
// step's input after the pass (step()), so a loop through one is not an algebraic loop.

// y' = u, integrated with a built-in solver (default euler).
class Integrator : public Block {
 public:
  explicit Integrator(double initial, std::string solver = "euler")
      : y_(initial), solver_name_(std::move(solver)), solver_(solve::make(solver_name_)) {}
  Values outputs() const override { return {{"y", y_}}; }
  Values step(const Context& ctx, const Values& in) override {
    solve::State s = {y_};
    solver_->step(s, ctx.t, ctx.dt, [&](double, const solve::State&) {
      return solve::State{get_or(in, "u")};
    });
    y_ = s[0];
    return outputs();
  }

 private:
  double y_;
  std::string solver_name_;
  std::unique_ptr<solve::Solver> solver_;
};

// A first-order lag, tau y' = u - y, integrated with a built-in solver (default euler). tau 0: a one-step delay.
class Lag : public Block {
 public:
  explicit Lag(double tau, std::string solver = "euler")
      : tau_(tau), solver_name_(std::move(solver)), solver_(solve::make(solver_name_)) {}
  Values outputs() const override { return {{"y", y_}}; }
  Values step(const Context& ctx, const Values& in) override {
    const double u = get_or(in, "u");
    if (tau_ <= 0) {
      y_ = u;
    } else {
      solve::State s = {y_};
      solver_->step(s, ctx.t, ctx.dt, [&](double, const solve::State& x) {
        return solve::State{(u - x[0]) / tau_};
      });
      y_ = s[0];
    }
    return outputs();
  }

 private:
  double tau_;
  double y_ = 0.0;
  std::string solver_name_;
  std::unique_ptr<solve::Solver> solver_;
};

// Its input as it was `steps` clock steps ago (initial before then): any value, not only
// numbers. The discrete-time z^-n.
class Delay : public Block {
 public:
  Delay(double steps, double initial) : n_(std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(steps)))), initial_(initial) {}
  Values outputs() const override {
    if (past_.size() < n_) return {{"y", initial_}};
    return {{"y", past_.front()}};
  }
  Values step(const Context&, const Values& in) override {
    auto u = in.find("u");
    past_.push_back(u == in.end() ? Value{0.0} : u->second);
    if (past_.size() > n_) past_.pop_front();
    return {};
  }

 private:
  std::size_t n_;
  double initial_;
  std::deque<Value> past_;
};

}  // namespace

void add_simulator(Registry& registry) {
  auto without_feedthrough = [&](Template t) {
    t.def.feedthrough = false;
    t.def.elementwise = t.def.type != "delay";  // a vector: one state per element; Delay moves whole values
    registry.add(std::move(t));
  };
  without_feedthrough(make_template(
      "control", "integrator", "Integrator", {"u"}, {"y"},
      {{.id = "initial", .default_value = 0.0}, solve::param("euler")},
      [](const Values& p, const Config&) {
        return std::make_unique<Integrator>(get_or(p, "initial"), get_text(p, "solver", "euler"));
      },
      "Accumulates its input over time. A feedback loop may pass through it."));
  without_feedthrough(make_template(
      "control", "lag", "Lag", {"u"}, {"y"},
      {{.id = "tau", .default_value = 1.0}, solve::param("euler")},
      [](const Values& p, const Config&) {
        return std::make_unique<Lag>(get_or(p, "tau"), get_text(p, "solver", "euler"));
      },
      "Smooths its input with a time constant: a first-order lag."));
  auto delay = make_template(
      "control", "delay", "Delay", {"u"}, {"y"}, {{"steps", 1.0}, {"initial", 0.0}},
      [](const Values& p, const Config&) { return std::make_unique<Delay>(get_or(p, "steps"), get_or(p, "initial")); },
      "Its input from a number of clock steps ago; any value.");
  delay.def.inputs[0].type = delay.def.outputs[0].type = "any";  // delays any value, not only numbers
  without_feedthrough(std::move(delay));
}

}  // namespace paperboat::toolkit::control
