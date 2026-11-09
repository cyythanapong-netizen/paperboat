// dashboard/lamp.

#include <cmath>
#include <string>
#include <string_view>

#include "common.hpp"
#include "dashboard/lamp/icon.hpp"

namespace paperboat::toolkit::dashboard {
namespace {

// How `in` is compared with `threshold`: the same choices, in the same order, as base/compare.
enum class Op { Above, AtLeast, Below, AtMost, Equal, NotEqual };

bool lit(Op op, double value, double threshold) {
  switch (op) {  // NaN lights nothing (every comparison but != is false; != NaN is no reading)
    case Op::Above: return value > threshold;
    case Op::AtLeast: return value >= threshold;
    case Op::Below: return value < threshold;
    case Op::AtMost: return value <= threshold;
    case Op::Equal: return value == threshold;
    case Op::NotEqual: return !std::isnan(value) && value != threshold;
  }
  return false;
}

// A Lamp: on while `in` meets the condition (`in` > `threshold` by default, so a boolean lights
// it); with `latch`, on from the first time it does until the run ends. It outputs its state
// as `y` (1 or 0), which the Dashboard shows and a flow may use (a Step flow's `when` → `stop`).
class Lamp final : public Block {
 public:
  Lamp(Op op, double threshold, bool latch) : op_(op), threshold_(threshold), latch_(latch) {}

  Values step(const Context&, const Values& in) override {
    const double* value = get_if<double>(in, "in");
    const bool now = value != nullptr && lit(op_, *value, threshold_);
    tripped_ = now || (latch_ && tripped_);
    return {{"y", tripped_ ? 1.0 : 0.0}};
  }

  // The threshold may be bound to a tunable variable; a latched lamp stays tripped.
  bool set_param(std::string_view id, double value) override {
    if (id != "threshold") return false;
    threshold_ = value;
    return true;
  }

 private:
  Op op_;
  double threshold_;
  bool latch_;
  bool tripped_ = false;
};

ParamDef op_param() {
  return {.id = "op", .type = "enum", .default_text = ">", .options = {">", ">=", "<", "<=", "==", "!="}};
}

Template lamp_template() {
  Template t;
  t.name = "dashboard/lamp";
  t.def = {
      .type = "lamp",
      .label = "Lamp",
      .kind = Kind::Monitor,
      .inputs = {{"in", "signal"}},
      .outputs = {{"y", "signal"}},
      .params = {
          {.id = "threshold", .default_value = 0.5},
          op_param(),
          {.id = "latch", .default_value = 0.0, .type = "boolean"},
          ParamDef{.id = "label", .type = "text", .default_text = ""},
          {.id = "color", .type = "enum", .default_text = "signal", .options = {"signal", "danger"}},
      },
      .instrument = "lamp",
      .description = "Lights on the Dashboard while its input passes a threshold; can latch until the run ends.",
      .view = {.compact = true, .glyph = "lamp", .value = "{op} {threshold}", .icon = lamp_icon()},
  };
  t.make = [](const Values& p, const Config& config) {
    // The op by name (config), else by its index in the options (what an expression sees).
    const ParamDef ops = op_param();
    int op = config.contains("op") ? ops.option(config.at("op")) : static_cast<int>(get_or(p, "op", 0.0));
    if (op < 0 || op >= static_cast<int>(ops.options.size())) op = 0;
    return std::make_unique<Lamp>(static_cast<Op>(op), get_or(p, "threshold", 0.5), get_or(p, "latch", 0.0) != 0.0);
  };
  return t;
}

}  // namespace

void add_lamp(Registry& registry) {
  registry.add(lamp_template());
}

}  // namespace paperboat::toolkit::dashboard
