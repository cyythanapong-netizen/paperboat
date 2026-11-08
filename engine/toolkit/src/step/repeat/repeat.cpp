// step/repeat.

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

// Leaves by `loop` `times` times, then by `done`, counting how often the run arrives.
// Wire the end of the loop back to its input. After `done` it counts from zero again.
// `count` is the lap the path is on (1 to times); after done, how many it went round.
class Repeat final : public Step {
 public:
  explicit Repeat(double times) : times_(times) {}
  void enter(const Context&, const Values&) override { ++count_; }
  std::string tick(const Context&, const Values&) override {
    if (count_ <= times_) {
      lap_ = count_;
      return "loop";
    }
    count_ = 0;
    return "done";
  }
  Values outputs() const override { return {{"count", static_cast<double>(lap_)}}; }

 private:
  double times_;
  int count_ = 0;
  int lap_ = 0;
};

}  // namespace

void add_repeat(Registry& registry) {
  registry.add(make_step_template("repeat", "Repeat", {step_port("in")},
                                  {step_port("loop"), step_port("done"), {"count"}},
                                   {{"times", 3.0}}, [](const Values& p, const Config&) {
                                     return std::make_unique<Repeat>(get_or(p, "times"));
                                   }, "Leaves by loop a number of times, then by done; count is the lap."));
}

}  // namespace paperboat::toolkit::step
