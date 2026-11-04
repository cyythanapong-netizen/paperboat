// The path of a Step flow: where paths start, wait, branch and loop.

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// Leaves by `loop` `times` times, then by `done`, counting how often the run arrives.
// Wire the end of the loop back to its input. After `done` it counts from zero again.
// `count` is the lap the path is on (1 to times); after done, how many it went round.
class Repeat final : public Step {
 public:
  explicit Repeat(double times) : times_(times) {}
  void enter(const Context&, const Values&) override { ++count_; }
  std::string tick(const Context&, const Values&) override {
    if (count_ <= times_) {
      lap_ = count_;
      return "loop";
    }
    count_ = 0;
    return "done";
  }
  Values outputs() const override { return {{"count", static_cast<double>(lap_)}}; }

 private:
  double times_;
  int count_ = 0;
  int lap_ = 0;
};

// Leaves by `loop` while `cond` holds (is positive), else by `done`. Wire the end of the
// loop back to its input; the condition is read each time the path arrives.
struct While final : Step {
  std::string tick(const Context&, const Values& in) override { return get_or(in, "cond") > 0 ? "loop" : "done"; }
};

// A value as a line of text: a number, text, a short list, a table's size, a handle's kind.
std::string say(const Value& v) {
  char number[32];
  auto num = [&](double x) {
    std::snprintf(number, sizeof number, "%g", x);
    return std::string(number);
  };
  if (const double* x = std::get_if<double>(&v)) return num(*x);
  if (const std::string* text = std::get_if<std::string>(&v)) return *text;
  if (const List* list = std::get_if<List>(&v)) {
    constexpr std::size_t kShown = 8;
    std::string out = "[";
    for (std::size_t i = 0; i < std::min(list->size(), kShown); ++i) out += (i ? ", " : "") + num((*list)[i]);
    if (list->size() > kShown) out += ", … " + std::to_string(list->size()) + " in all";
    return out + "]";
  }
  if (const Table* table = std::get_if<Table>(&v))
    return "table " + std::to_string(table->size()) + " × " + std::to_string(table->empty() ? 0 : table->front().size());
  return "(" + std::get<Handle>(v).kind + ")";
}

// Writes a line to the Console as the path passes: its text, then `value` if wired.
class Say final : public Step {
 public:
  explicit Say(std::string text) : text_(std::move(text)) {}
  void enter(const Context& ctx, const Values& in) override {
    auto it = in.find("value");
    if (it == in.end()) ctx.log(text_);
    else ctx.log(text_.empty() ? say(it->second) : text_ + " " + say(it->second));
  }
  std::string tick(const Context&, const Values&) override { return "next"; }

 private:
  std::string text_;
};

// Waits until `paths` paths have arrived, then goes on as one: the last to arrive leaves
// by next, the others end here (by joined, which nothing is wired to). Several paths may
// be in it at once. More arrivals start the next count.
class Join final : public Step {
 public:
  explicit Join(double paths) : paths_(std::max(1, static_cast<int>(std::lround(paths)))) {}
  void enter(const Context&, const Values&) override { ++arrived_; }
  std::string tick(const Context&, const Values&) override {
    if (arrived_ < paths_) return "";
    const bool first = released_++ == 0;
    if (released_ == paths_) {
      arrived_ -= paths_;
      released_ = 0;
    }
    return first ? "next" : "joined";
  }
  bool shared() const override { return true; }

 private:
  int paths_;
  int arrived_ = 0;
  int released_ = 0;
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
  registry.add(make_step_template(
      "when", "When", {{"cond"}}, {step_port("next")},
      {{.id = "on", .type = "enum", .default_text = "rise", .options = {"rise", "fall"}},
       {.id = "running", .type = "enum", .default_text = "keep", .options = {"keep", "restart"}}},
      [](const Values& p, const Config&) {
        return std::make_unique<When>(get_or(p, "on") == 1, get_or(p, "running") == 1);  // by index in options
      }));
  registry.add(make_step_template("wait", "Wait", {step_port("in")}, {step_port("next")}, {{"seconds", 1.0}},
                                  [](const Values& p, const Config&) {
                                    return std::make_unique<Wait>(get_or(p, "seconds"));
                                  }));
  registry.add(make_step_template("repeat", "Repeat", {step_port("in")},
                                  {step_port("loop"), step_port("done"), {"count"}},
                                  {{"times", 3.0}}, [](const Values& p, const Config&) {
                                    return std::make_unique<Repeat>(get_or(p, "times"));
                                  }));
  registry.add(make_step_template("while", "While", {step_port("in"), {"cond"}}, {step_port("loop"), step_port("done")},
                                  {}, [](const Values&, const Config&) { return std::make_unique<While>(); }));
  registry.add(make_step_template("say", "Say", {step_port("in"), {"value", "any"}}, {step_port("next")},
                                  {{.id = "text", .type = "text", .default_text = "hello"}},
                                  [](const Values&, const Config& c) {
                                    auto it = c.find("text");
                                    return std::make_unique<Say>(it == c.end() ? "hello" : it->second);
                                  }));
  registry.add(make_step_template("join", "Join", {step_port("in")}, {step_port("next")}, {{"paths", 2.0}},
                                  [](const Values& p, const Config&) {
                                    return std::make_unique<Join>(get_or(p, "paths"));
                                  }));
  registry.add(make_step_template("stop", "Stop", {step_port("in")}, {}, {},
                                  [](const Values&, const Config&) { return std::make_unique<Stop>(); }));
}

}  // namespace paperboat::toolkit::step
