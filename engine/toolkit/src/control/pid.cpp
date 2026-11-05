// PID controller. Each instance keeps its own integral and previous error.

#include <optional>

#include "common.hpp"

namespace paperboat::toolkit::control {
namespace {

class Pid : public Block {
 public:
  Pid(double kp, double ki, double kd) : kp_(kp), ki_(ki), kd_(kd) {}

  Values step(const Context& ctx, const Values& in) override {
    double e = get_or(in, "e");
    integral_ += e * ctx.dt;  // forward Euler
    // Backward difference; no previous error on the first step, so no derivative kick.
    double derivative = (prev_ && ctx.dt > 0) ? (e - *prev_) / ctx.dt : 0.0;
    prev_ = e;
    return {{"u", kp_ * e + ki_ * integral_ + kd_ * derivative}};
  }

 private:
  double kp_, ki_, kd_;
  double integral_ = 0.0;
  std::optional<double> prev_;
};

}  // namespace

void add_pid(Registry& registry) {
  registry.add(make_template(
      "control", "pid", "PID", {"e"}, {"u"}, {{"kp", 1.0}, {"ki", 0.0}, {"kd", 0.0}},
      [](const Values& p, const Config&) {
        return std::make_unique<Pid>(get_or(p, "kp"), get_or(p, "ki"), get_or(p, "kd"));
      }, "Drives an error to zero: proportional, integral and derivative terms."));
}

}  // namespace paperboat::toolkit::control
