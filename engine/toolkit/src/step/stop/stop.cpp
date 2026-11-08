// step/stop.

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

// Ends the whole run, every path, when a path arrives.
struct Stop final : Step {
  std::string tick(const Context&, const Values&) override { return ""; }
  bool ends_run() const override { return true; }
};

}  // namespace

void add_stop(Registry& registry) {
  registry.add(make_step_template("stop", "Stop", {step_port("in")}, {}, {},
                                  [](const Values&, const Config&) { return std::make_unique<Stop>(); },
                                  "Ends the whole run."));
}

}  // namespace paperboat::toolkit::step
