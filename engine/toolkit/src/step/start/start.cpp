// step/start.

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

// A hat: starts the run's first path, once, at the start.
class Start final : public Step {
 public:
  std::string tick(const Context&, const Values&) override {
    if (fired_) return "";
    fired_ = true;
    return "next";
  }
  bool may_start() const override { return !fired_; }

 private:
  bool fired_ = false;
};

}  // namespace

void add_start(Registry& registry) {
  registry.add(make_step_template("start", "Start", {}, {step_port("next")}, {},
                                  [](const Values&, const Config&) { return std::make_unique<Start>(); },
                                  "Starts a path once, when the run begins."));
}

}  // namespace paperboat::toolkit::step
