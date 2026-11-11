// step/signal_step: the bridge from the signal layer to a step path.
//
// A hat with one signal input: a 0/1 (or boolean) signal turns into a step wire. It starts a
// path when the signal rises (or, with `on = fall`, when it drops), just like a key press in
// When, but it is the one-purpose converter the palette names by what it does.

#include <memory>
#include <string>

#include "common.hpp"
#include "step/flow_shared.hpp"

namespace paperboat::toolkit::step {
namespace {

class OnSignal final : public Step {
 public:
  explicit OnSignal(bool on_fall) : on_fall_(on_fall) {}
  std::string tick(const Context&, const Values& in) override {
    const bool on = get_or(in, "signal") > 0;
    const bool fired = on_fall_ ? (was_on_ && !on) : (on && !was_on_);
    was_on_ = on;
    return fired ? "next" : "";
  }
  bool may_start() const override { return true; }

 private:
  bool on_fall_;
  bool was_on_ = false;
};

}  // namespace

void add_signal_step(Registry& registry) {
  registry.add(make_step_template(
      "signal_step", "Signal → Step", {{"signal"}}, {step_port("next")},
      {{.id = "on", .type = "enum", .default_text = "rise", .options = {"rise", "fall"}}},
      [](const Values& p, const Config&) { return std::make_unique<OnSignal>(get_or(p, "on") == 1); },
      "Turns a 0/1 (boolean) signal into a step: starts a path when it rises (or falls)."));
}

}  // namespace paperboat::toolkit::step
