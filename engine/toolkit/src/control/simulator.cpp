// Dynamic blocks for the flow: an integrator and a first-order lag. Plants use the
// integrators in paperboat/toolkit/control/simulator.hpp directly.

#include "common.hpp"
#include "paperboat/toolkit/control/simulator.hpp"

namespace paperboat::toolkit::control {
namespace {

class Integrator : public Block {
 public:
  explicit Integrator(double initial) : y_(initial) {}
  Values step(const Context& ctx, const Values& in) override {
    double y = y_;  // output the state before this step's input is added
    y_ = euler_step<1>({y_}, ctx.t, ctx.dt, [&](double, const State<1>&) {
      return State<1>{get_or(in, "u")};
    })[0];
    return {{"y", y}};
  }

 private:
  double y_;
};

class Lag : public Block {
 public:
  explicit Lag(double tau) : tau_(tau) {}
  Values step(const Context& ctx, const Values& in) override {
    double u = get_or(in, "u");
    if (tau_ <= 0) return {{"y", y_ = u}};  // no lag: pass through
    y_ = euler_step<1>({y_}, ctx.t, ctx.dt, [&](double, const State<1>& x) {
      return State<1>{(u - x[0]) / tau_};
    })[0];
    return {{"y", y_}};
  }

 private:
  double tau_;
  double y_ = 0.0;
};

}  // namespace

void add_simulator(Registry& registry) {
  registry.add(make_template(
      "control", "integrator", "Integrator", {"u"}, {"y"}, {{"initial", 0.0}},
      [](const Values& p, const Config&) { return std::make_unique<Integrator>(get_or(p, "initial")); }));

  registry.add(make_template(
      "control", "lag", "Lag", {"u"}, {"y"}, {{"tau", 1.0}},
      [](const Values& p, const Config&) { return std::make_unique<Lag>(get_or(p, "tau")); }));
}

}  // namespace paperboat::toolkit::control
