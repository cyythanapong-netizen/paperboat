// signal/clock.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "paperboat/toolkit/expression.hpp"

namespace paperboat::toolkit::signal {
namespace {

// The run time itself: f(t) = t (end a run after 10 s: Clock → Threshold → When → Stop).
class Clock final : public Generator {
 public:
  Clock() : Generator("t") {}
  double value(double t) const override { return t; }
};

}  // namespace

void add_clock(Registry& registry) {
  auto clock = define_block<Clock>("signal", generator_def("clock", "Clock", {}, "t"));
  clock.def.description = "The run's time, in seconds.";
  registry.add(std::move(clock));
}

}  // namespace paperboat::toolkit::signal
