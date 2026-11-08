// base/timeout.

#include <algorithm>
#include <deque>
#include <utility>

#include "common.hpp"
#include "base/timeout/icon.hpp"

namespace paperboat::toolkit::base {
namespace {

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

}  // namespace

void add_timeout(Registry& registry) {
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
  tout.def.view = {.compact = true, .glyph = "timer", .value = "{delay} ms", .icon = timer_icon()};
  tout.make = [](const Values& params, const Config&) {
    return std::make_unique<Timeout>(get_or(params, "delay", 1000.0));
  };
  registry.add(std::move(tout));
}

}  // namespace paperboat::toolkit::base
