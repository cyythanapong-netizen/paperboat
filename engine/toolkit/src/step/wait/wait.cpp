// step/wait.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"
#include "step/flow_shared.hpp"

namespace paperboat::toolkit::step {
namespace {

// Stays for `seconds` of run time.
class Wait final : public Step {
 public:
  explicit Wait(double seconds) : seconds_(seconds) {}
  void enter(const Context& ctx, const Values&) override { until_ = ctx.t + seconds_; }
  std::string tick(const Context& ctx, const Values&) override {
    // Half a step of slack so a 1 s wait at dt = 0.01 ends on the 100th step, not the 101st.
    return ctx.t + ctx.dt / 2 >= until_ ? "next" : "";
  }

 private:
  double seconds_;
  double until_ = 0.0;
};

}  // namespace

void add_wait(Registry& registry) {
  registry.add(make_step_template("wait", "Wait", {step_port("in")}, {step_port("next")}, {{"seconds", 1.0}},
                                   [](const Values& p, const Config&) {
                                     return std::make_unique<Wait>(get_or(p, "seconds"));
                                   }, "Holds the path for a while."));
}

}  // namespace paperboat::toolkit::step
