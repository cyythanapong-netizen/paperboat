// base/delay.

#include <algorithm>
#include <deque>
#include <utility>

#include "common.hpp"

namespace paperboat::toolkit::base {
namespace {

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

void add_delay(Registry& registry) {
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
