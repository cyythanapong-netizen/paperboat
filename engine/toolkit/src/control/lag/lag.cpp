// control/lag.

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "common.hpp"
#include "paperboat/toolkit/control/simulator.hpp"
#include "paperboat/toolkit/solve.hpp"
#include "control/simulator_shared.hpp"

namespace paperboat::toolkit::control {
namespace {

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

}  // namespace

void add_lag(Registry& registry) {
  add_without_feedthrough(registry, make_template(
      "control", "lag", "Lag", {"u"}, {"y"},
      {{.id = "tau", .default_value = 1.0}, solve::param("euler")},
      [](const Values& p, const Config&) {
        return std::make_unique<Lag>(get_or(p, "tau"), get_text(p, "solver", "euler"));
      },
      "Smooths its input with a time constant: a first-order lag."));
}

}  // namespace paperboat::toolkit::control
