// Dynamic blocks for the flow: an integrator, a first-order lag and a delay. Domains use
// the integrators in paperboat/toolkit/control/simulator.hpp directly.

#include <algorithm>
#include <cmath>
#include <deque>

#include "common.hpp"
#include "paperboat/toolkit/control/simulator.hpp"

namespace paperboat::toolkit::control {
namespace {

// Blocks without direct feedthrough: each outputs its state (outputs()), then takes the
// step's input after the pass (step()), so a loop through one is not an algebraic loop.

// y' = u, forward Euler: y(k+1) = y(k) + u(k) dt.
class Integrator : public Block {
 public:
  explicit Integrator(double initial) : y_(initial) {}
  Values outputs() const override { return {{"y", y_}}; }
  Values step(const Context& ctx, const Values& in) override {
    y_ = euler_step<1>({y_}, ctx.t, ctx.dt, [&](double, const State<1>&) {
      return State<1>{get_or(in, "u")};
    })[0];
    return outputs();
  }

 private:
  double y_;
};

// A first-order lag, tau y' = u - y, forward Euler. tau 0: a one-step delay.
class Lag : public Block {
 public:
  explicit Lag(double tau) : tau_(tau) {}
  Values outputs() const override { return {{"y", y_}}; }
  Values step(const Context& ctx, const Values& in) override {
    const double u = get_or(in, "u");
    if (tau_ <= 0) {
      y_ = u;
    } else {
      y_ = euler_step<1>({y_}, ctx.t, ctx.dt, [&](double, const State<1>& x) {
        return State<1>{(u - x[0]) / tau_};
      })[0];
    }
    return outputs();
  }

 private:
  double tau_;
  double y_ = 0.0;
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
    registry.add(std::move(t));
  };
  without_feedthrough(make_template(
      "control", "integrator", "Integrator", {"u"}, {"y"}, {{"initial", 0.0}},
      [](const Values& p, const Config&) { return std::make_unique<Integrator>(get_or(p, "initial")); }));
  without_feedthrough(make_template(
      "control", "lag", "Lag", {"u"}, {"y"}, {{"tau", 1.0}},
      [](const Values& p, const Config&) { return std::make_unique<Lag>(get_or(p, "tau")); }));
  auto delay = make_template(
      "control", "delay", "Delay", {"u"}, {"y"}, {{"steps", 1.0}, {"initial", 0.0}},
      [](const Values& p, const Config&) { return std::make_unique<Delay>(get_or(p, "steps"), get_or(p, "initial")); });
  delay.def.inputs[0].type = delay.def.outputs[0].type = "any";  // delays any value, not only numbers
  without_feedthrough(std::move(delay));
}

}  // namespace paperboat::toolkit::control
