// base/interval.

#include <algorithm>
#include <deque>
#include <utility>

#include "common.hpp"
#include "base/interval/icon.hpp"

namespace paperboat::toolkit::base {
namespace {

class Interval final : public Block {
 public:
  explicit Interval(double interval_ms) : default_interval_ms_(interval_ms) {}

  Values step(const Context& ctx, const Values& in) override {
    double period_ms = in.contains("interval") ? get_or(in, "interval") : default_interval_ms_;
    double period_s = std::max(0.001, period_ms / 1000.0);

    bool has_start = in.contains("start");
    bool start_sig = get_or(in, "start") > 0.0;
    bool stop_sig = get_or(in, "stop") > 0.0;

    if (first_step_) {
      first_step_ = false;
      if (has_start) {
        active_ = start_sig;
      } else {
        active_ = true;
      }
      next_fire_t_ = ctx.t + period_s;
    }

    if (stop_sig) {
      active_ = false;
    } else if (start_sig) {
      if (!active_) {
        active_ = true;
        next_fire_t_ = ctx.t + period_s;
      }
    }

    if (active_ && (ctx.t + 1e-9 >= next_fire_t_)) {
      next_fire_t_ += period_s;
      if (next_fire_t_ <= ctx.t) next_fire_t_ = ctx.t + period_s;
      return {{"y", 1.0}};
    }
    return {{"y", 0.0}};
  }

 private:
  double default_interval_ms_;
  bool first_step_ = true;
  bool active_ = false;
  double next_fire_t_ = 0.0;
};

}  // namespace

void add_interval(Registry& registry) {
  // base/interval
  Template intv;
  intv.name = "base/interval";
  intv.def.type = "interval";
  intv.def.label = "Interval";
  intv.def.kind = Kind::Source;
  intv.def.inputs = {{.id = "start", .type = "signal"}, {.id = "stop", .type = "signal"}};
  intv.def.outputs = {{.id = "y", .type = "signal"}};
  intv.def.params = {{.id = "interval", .default_value = 1000.0, .type = "number"}};
  intv.def.description = "Pulses 1.0 periodically every interval milliseconds.";
  intv.def.view = {.compact = true, .glyph = "pulse", .value = "{interval} ms", .icon = pulse_icon()};
  intv.make = [](const Values& params, const Config&) {
    return std::make_unique<Interval>(get_or(params, "interval", 1000.0));
  };
  registry.add(std::move(intv));
}

}  // namespace paperboat::toolkit::base
