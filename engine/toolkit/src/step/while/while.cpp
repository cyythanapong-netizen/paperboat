// step/while.

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

// Leaves by `loop` while `cond` holds (is positive), else by `done`. Wire the end of the
// loop back to its input; the condition is read each time the path arrives.
struct While final : Step {
  std::string tick(const Context&, const Values& in) override { return get_or(in, "cond") > 0 ? "loop" : "done"; }
};

}  // namespace

void add_while(Registry& registry) {
  registry.add(make_step_template("while", "While", {step_port("in"), {"cond"}}, {step_port("loop"), step_port("done")},
                                  {}, [](const Values&, const Config&) { return std::make_unique<While>(); },
                                  "Leaves by loop while its condition holds."));
}

}  // namespace paperboat::toolkit::step
