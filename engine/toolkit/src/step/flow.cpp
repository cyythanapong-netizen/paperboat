// The path of a Step flow: where paths start, wait, branch and loop.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"

namespace paperboat::toolkit::step {
namespace {

PortDef step_port(std::string id) { return {std::move(id), "step"}; }

Template make_step_template(std::string type, std::string label, std::vector<PortDef> inputs,
                            std::vector<PortDef> outputs, std::vector<ParamDef> params,
                            decltype(Template::make_step) make) {
  Template t;
  t.name = "step/" + type;
  t.def.type = std::move(type);
  t.def.label = std::move(label);
  t.def.kind = Kind::Function;
  t.def.inputs = std::move(inputs);
  t.def.outputs = std::move(outputs);
  t.def.params = std::move(params);
  t.make_step = std::move(make);
  return t;
}

// A hat: starts the run's first path, once, at the start.
class Start final : public Step {
 public:
  std::string tick(const Context&, const Values&) override {
    if (fired_) return "";
    fired_ = true;
    return "next";
  }
  bool may_start() const override { return !fired_; }

 private:
  bool fired_ = false;
};

// A hat: starts a path each time `cond` rises (from not positive to positive), such as a
// key press, alongside whatever else is running. Like Scratch's "when key pressed".
class When final : public Step {
 public:
  std::string tick(const Context&, const Values& in) override {
    const bool on = get_or(in, "cond") > 0;
    const bool rose = on && !was_on_;
    was_on_ = on;
    return rose ? "next" : "";
  }
  bool may_start() const override { return true; }

 private:
  bool was_on_ = false;
};

// Stays for `seconds` of run time.
class Wait final : public Step {
 public:
  explicit Wait(double seconds) : seconds_(seconds) {}
  void enter(const Context& ctx, const Values&) override { until_ = ctx.t + seconds_; }
  std::string tick(const Context& ctx, const Values&) override {
    // Half a step of slack so a 1 s wait at dt = 0.01 ends on the 100th step, not the 101st.
    return ctx.t + ctx.dt / 2 >= until_ ? "next" : "";
  }

 private:
  double seconds_;
  double until_ = 0.0;
};

// Branches on a signal: yes while cond is positive (a threshold or key gives 1 or 0).
struct If final : Step {
  std::string tick(const Context&, const Values& in) override { return get_or(in, "cond") > 0 ? "yes" : "no"; }
};

// Leaves by `loop` `times` times, then by `done`, counting how often the run arrives.
// Wire the end of the loop back to its input. After `done` it counts from zero again.
class Repeat final : public Step {
 public:
  explicit Repeat(double times) : times_(times) {}
  void enter(const Context&, const Values&) override { ++count_; }
  std::string tick(const Context&, const Values&) override {
    if (count_ <= times_) return "loop";
    count_ = 0;
    return "done";
  }

 private:
  double times_;
  int count_ = 0;
};

// Ends the whole run, every path, when a path arrives.
struct Stop final : Step {
  std::string tick(const Context&, const Values&) override { return ""; }
  bool ends_run() const override { return true; }
};

}  // namespace

void add_flow(Registry& registry) {
  registry.add(make_step_template("start", "Start", {}, {step_port("next")}, {},
                                  [](const Values&, const Config&) { return std::make_unique<Start>(); }));
  registry.add(make_step_template("when", "When", {{"cond"}}, {step_port("next")}, {},
                                  [](const Values&, const Config&) { return std::make_unique<When>(); }));
  registry.add(make_step_template("wait", "Wait", {step_port("in")}, {step_port("next")}, {{"seconds", 1.0}},
                                  [](const Values& p, const Config&) {
                                    return std::make_unique<Wait>(get_or(p, "seconds"));
                                  }));
  registry.add(make_step_template("if", "If", {step_port("in"), {"cond"}}, {step_port("yes"), step_port("no")}, {},
                                  [](const Values&, const Config&) { return std::make_unique<If>(); }));
  registry.add(make_step_template("repeat", "Repeat", {step_port("in")}, {step_port("loop"), step_port("done")},
                                  {{"times", 3.0}}, [](const Values& p, const Config&) {
                                    return std::make_unique<Repeat>(get_or(p, "times"));
                                  }));
  registry.add(make_step_template("stop", "Stop", {step_port("in")}, {}, {},
                                  [](const Values&, const Config&) { return std::make_unique<Stop>(); }));
}

}  // namespace paperboat::toolkit::step
