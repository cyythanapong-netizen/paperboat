// Time additions: now, interval, timeout, delay.

#include <algorithm>
#include <deque>
#include <utility>

#include "common.hpp"
#include "curve_icons.hpp"

namespace paperboat::toolkit::base {
namespace {

class Now final : public Block {
 public:
  Values step(const Context& ctx, const Values&) override {
    return {{"y", ctx.t * 1000.0}};
  }
};

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

class Timeout final : public Block {
 public:
  explicit Timeout(double delay_ms) : delay_ms_(delay_ms) {}

  Values step(const Context& ctx, const Values& in) override {
    double delay_s = std::max(0.0, delay_ms_ / 1000.0);
    bool reset = get_or(in, "reset") > 0.0;

    if (first_step_ || reset) {
      first_step_ = false;
      start_t_ = ctx.t;
    }

    bool expired = (ctx.t - start_t_) + 1e-9 >= delay_s;
    return {{"y", expired ? 1.0 : 0.0}};
  }

 private:
  double delay_ms_;
  bool first_step_ = true;
  double start_t_ = 0.0;
};

class Delay final : public Block {
 public:
  explicit Delay(double delay_ms) : delay_ms_(delay_ms) {}

  Values step(const Context& ctx, const Values& in) override {
    double delay_s = std::max(0.0, delay_ms_ / 1000.0);

    if (delay_s == 0.0) {
      current_ = in.contains("in") ? in.at("in") : Value{0.0};
      return {{"y", current_}};
    }

    if (in.contains("in")) {
      buffer_.push_back({ctx.t + delay_s, in.at("in")});
    }

    while (!buffer_.empty() && buffer_.front().first <= ctx.t + 1e-9) {
      current_ = std::move(buffer_.front().second);
      buffer_.pop_front();
    }

    return {{"y", current_}};
  }

 private:
  double delay_ms_;
  Value current_{0.0};
  std::deque<std::pair<double, Value>> buffer_;
};

}  // namespace

void add_time(Registry& registry) {
  // base/now
  Template now_t;
  now_t.name = "base/now";
  now_t.def.type = "now";
  now_t.def.label = "Now";
  now_t.def.kind = Kind::Source;
  now_t.def.outputs = {{.id = "y", .type = "signal"}};
  now_t.def.description = "Current run clock in milliseconds.";
  now_t.def.view = {.compact = true, .glyph = "now"};
  now_t.make = [](const Values&, const Config&) { return std::make_unique<Now>(); };
  registry.add(std::move(now_t));

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
  intv.def.view = {.compact = true, .glyph = "pulse", .value = "{interval} ms", .icon = icons::pulse()};
  intv.make = [](const Values& params, const Config&) {
    return std::make_unique<Interval>(get_or(params, "interval", 1000.0));
  };
  registry.add(std::move(intv));

  // base/timeout
  Template tout;
  tout.name = "base/timeout";
  tout.def.type = "timeout";
  tout.def.label = "Timeout";
  tout.def.kind = Kind::Source;
  tout.def.inputs = {{.id = "reset", .type = "signal"}};
  tout.def.outputs = {{.id = "y", .type = "signal"}};
  tout.def.params = {{.id = "delay", .default_value = 1000.0, .type = "number"}};
  tout.def.description = "Outputs 1.0 after a delay in milliseconds has elapsed.";
  tout.def.view = {.compact = true, .glyph = "timer", .value = "{delay} ms", .icon = icons::timer()};
  tout.make = [](const Values& params, const Config&) {
    return std::make_unique<Timeout>(get_or(params, "delay", 1000.0));
  };
  registry.add(std::move(tout));

  // base/delay
  Template del;
  del.name = "base/delay";
  del.def.type = "delay";
  del.def.label = "Delay";
  del.def.kind = Kind::Function;
  del.def.feedthrough = false;
  del.def.inputs = {{.id = "in", .type = "any"}};
  del.def.outputs = {{.id = "y", .type = "any"}};
  del.def.params = {{.id = "delay", .default_value = 1000.0, .type = "number"}};
  del.def.description = "Delays values by delay milliseconds.";
  del.def.view = {.compact = true, .glyph = "z⁻¹", .value = "{delay} ms"};
  del.make = [](const Values& params, const Config&) {
    return std::make_unique<Delay>(get_or(params, "delay", 1000.0));
  };
  registry.add(std::move(del));
}

}  // namespace paperboat::toolkit::base
