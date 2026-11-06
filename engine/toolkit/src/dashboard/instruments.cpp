// The dashboard package: monitors, the blocks that show a flow's values on the Dashboard.
// Each declares its instrument; the core sends what it shows as `signal` events. A
// monitor that needs to turn its input into something showable (an image into a picture)
// returns it as output "value" (see opencv/view); otherwise the core sends input "in".

#include "common.hpp"

namespace paperboat::toolkit::dashboard {
namespace {

struct Passive final : Block {
  Values step(const Context&, const Values&) override { return {}; }
};

class Switch final : public Block {
 public:
  explicit Switch(std::string key, int mode, double initial)
      : key_(std::move(key)), mode_(mode), initial_(initial > 0.0), state_(initial > 0.0) {}

  Values step(const Context& ctx, const Values&) override {
    // The dashboard control sends the node id; `key` is an extra binding (a keyboard key).
    const auto held = [&](const std::string& key) {
      return ctx.keys_down != nullptr && !key.empty() && ctx.keys_down->contains(key);
    };
    const bool down = held(std::string(ctx.node)) || held(key_);
    if (first_) {
      first_ = false;
      state_ = initial_;
      last_down_ = down;
    } else {
      if (mode_ == 1) {
        state_ = down;
      } else {
        if (down && !last_down_) {
          state_ = !state_;
        }
      }
      last_down_ = down;
    }
    double val = state_ ? 1.0 : 0.0;
    return {{"y", val}, {"value", val}};
  }

 private:
  std::string key_;
  int mode_;
  bool initial_;
  bool state_;
  bool first_ = true;
  bool last_down_ = false;
};

Template monitor(std::string type, std::string label, std::string instrument, std::string description) {
  Template t;
  t.name = "dashboard/" + type;
  t.def = {.type = std::move(type), .label = std::move(label), .kind = Kind::Monitor, .inputs = {{"in", "any"}},
           .outputs = {}, .params = {}, .instrument = std::move(instrument), .description = std::move(description)};
  t.make = [](const Values&, const Config&) { return std::make_unique<Passive>(); };
  return t;
}

Template switch_template() {
  Template t;
  t.name = "dashboard/switch";
  t.def = {
      .type = "switch",
      .label = "Switch",
      .kind = Kind::Monitor,
      .inputs = {},
      .outputs = {{"y", "signal"}},
      .params = {
          {.id = "mode", .type = "enum", .default_text = "toggle", .options = {"toggle", "momentary"}},
          {.id = "initial", .default_value = 1.0, .type = "boolean"},
          ParamDef{.id = "key", .default_value = 0.0, .type = "text", .default_text = ""},
      },
      .instrument = "switch",
      .description = "A dashboard switch: toggle (click to flip) or momentary (press and hold).",
      .view = {.compact = true, .glyph = "switch"},
  };
  t.make = [](const Values& p, const Config& config) {
    const int mode = config.contains("mode") && config.at("mode") == "momentary" ? 1 : 0;
    double initial = get_or(p, "initial", 1.0);
    std::string key = config.contains("key") ? config.at("key") : "";
    return std::make_unique<Switch>(std::move(key), mode, initial);
  };
  return t;
}

}  // namespace

void add_instruments(Registry& registry) {
  registry.add(monitor("graph", "Graph", "graph", "Plots its input over time on the Dashboard."));
  registry.add(monitor("display", "Display", "display", "Shows its latest value on the Dashboard."));
  registry.add(switch_template());
}

}  // namespace paperboat::toolkit::dashboard
