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
  clock.def.view = {.compact = true, .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"ellipse","x":25,"y":20,"w":50,"h":60},{"t":"line","p":[[50,50],[50,30]]},{"t":"line","p":[[50,50],[65,60]]},{"t":"line","p":[[50,20],[50,25]],"weight":"thin"},{"t":"line","p":[[75,50],[70,50]],"weight":"thin"},{"t":"line","p":[[50,80],[50,75]],"weight":"thin"},{"t":"line","p":[[25,50],[30,50]],"weight":"thin"}]})")};  // a clock face
  registry.add(std::move(clock));
}

}  // namespace paperboat::toolkit::signal
