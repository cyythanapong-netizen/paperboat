// step/when.

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

// A hat: starts a path each time `cond` rises (from not positive to positive), such as a
// key press, alongside whatever else is running; or, with `on` = fall, each time it falls
// (a key released). Like Scratch's "when key pressed". If it fires while its path still
// runs, `running` keeps that path going or restarts it from the top.
class When final : public Step {
 public:
  When(bool on_fall, bool restart) : on_fall_(on_fall), restart_(restart) {}
  std::string tick(const Context&, const Values& in) override {
    const bool on = get_or(in, "cond") > 0;
    const bool fired = on_fall_ ? (was_on_ && !on) : (on && !was_on_);
    was_on_ = on;
    return fired ? "next" : "";
  }
  bool may_start() const override { return true; }
  bool restarts() const override { return restart_; }

 private:
  bool on_fall_;
  bool restart_;
  bool was_on_ = false;
};

}  // namespace

void add_when(Registry& registry) {
  registry.add(make_step_template(
      "when", "When", {{"cond"}}, {step_port("next")},
      {{.id = "on", .type = "enum", .default_text = "rise", .options = {"rise", "fall"}},
       {.id = "running", .type = "enum", .default_text = "keep", .options = {"keep", "restart"}}},
       [](const Values& p, const Config&) {
         return std::make_unique<When>(get_or(p, "on") == 1, get_or(p, "running") == 1);  // by index in options
       }, "Starts a path each time its condition rises (or falls): a key, a threshold."));
}

}  // namespace paperboat::toolkit::step
