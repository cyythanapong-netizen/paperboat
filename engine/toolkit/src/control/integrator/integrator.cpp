// control/integrator.

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

// Blocks without direct feedthrough: each outputs its state (outputs()), then takes the
// step's input after the pass (step()), so a loop through one is not an algebraic loop.

// y' = u, integrated with a built-in solver (default: the run's).
class Integrator : public Block {
 public:
  explicit Integrator(double initial, std::string solver = "auto")
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

}  // namespace

void add_integrator(Registry& registry) {
  add_without_feedthrough(registry, make_template(
      "control", "integrator", "Integrator", {"u"}, {"y"},
      {{.id = "initial", .default_value = 0.0}, solve::param()},
      [](const Values& p, const Config&) {
        return std::make_unique<Integrator>(get_or(p, "initial"), get_text(p, "solver", "auto"));
      },
      "Accumulates its input over time. A feedback loop may pass through it."));
}

}  // namespace paperboat::toolkit::control
