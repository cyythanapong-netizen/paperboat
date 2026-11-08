// control/delay.

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

// Its input as it was `steps` clock steps ago (initial before then): any value, not only
// numbers. The discrete-time z^-n.
class Delay : public Block {
 public:
  Delay(double steps, double initial) : n_(std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(steps)))), initial_(initial) {}
  Values outputs() const override {
    if (past_.size() < n_) return {{"y", initial_}};
    return {{"y", past_.front()}};
  }
  Values step(const Context&, const Values& in) override {
    auto u = in.find("u");
    past_.push_back(u == in.end() ? Value{0.0} : u->second);
    if (past_.size() > n_) past_.pop_front();
    return {};
  }

 private:
  std::size_t n_;
  double initial_;
  std::deque<Value> past_;
};

}  // namespace

void add_delay(Registry& registry) {
  auto delay = make_template(
      "control", "delay", "Delay", {"u"}, {"y"}, {{"steps", 1.0}, {"initial", 0.0}},
      [](const Values& p, const Config&) { return std::make_unique<Delay>(get_or(p, "steps"), get_or(p, "initial")); },
      "Its input from a number of clock steps ago; any value.");
  delay.def.inputs[0].type = delay.def.outputs[0].type = "any";
  // delays any value, not only numbers
  add_without_feedthrough(registry, std::move(delay));
}

}  // namespace paperboat::toolkit::control
