#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "paperboat/toolkit/bases.hpp"
#include "paperboat/toolkit/descriptor.hpp"

using namespace paperboat::toolkit;

namespace {

std::unique_ptr<Step> make(const std::string& name, const Values& params = {}) {
  const Template* t = builtins().find(name);
  REQUIRE(t != nullptr);
  REQUIRE(t->make_step);
  CHECK_FALSE(t->make);
  Values resolved;
  for (const auto& p : t->def.params) resolved[p.id] = get_or(params, p.id, p.default_value);
  return t->make_step(resolved, t->config);
}

}  // namespace

TEST_CASE("step templates have step ports, math and control ones don't") {
  CHECK(has_step_ports(builtins().find("step/wait")->def));
  CHECK(has_step_ports(builtins().find("step/start")->def));
  CHECK_FALSE(has_step_ports(builtins().find("math/threshold")->def));
  CHECK_FALSE(has_step_ports(builtins().find("math/gain")->def));
  // If (base, magic) in its step form reads its condition from a signal port.
  const auto step_if = in_form(builtins().find("base/if")->def, "step");
  CHECK(has_step_ports(step_if));
  REQUIRE(step_if.inputs.size() == 2);
  CHECK(step_if.inputs[1].id == "cond");
  CHECK(step_if.inputs[1].type == "signal");
  CHECK_FALSE(has_step_ports(builtins().find("base/if")->def));  // its default form: signal
}

TEST_CASE("step/start starts one path, once") {
  auto start = make("step/start");
  CHECK(start->may_start());
  CHECK(start->tick({}, {}) == "next");
  CHECK(start->tick({}, {}).empty());
  CHECK_FALSE(start->may_start());
}

TEST_CASE("step/when starts a path each time cond rises") {
  auto when = make("step/when");
  CHECK(when->may_start());
  CHECK(when->tick({}, {}).empty());  // unwired: never
  CHECK(when->tick({}, {{"cond", 1}}) == "next");
  CHECK(when->tick({}, {{"cond", 1}}).empty());  // still held: no new path
  CHECK(when->tick({}, {{"cond", 0}}).empty());
  CHECK(when->tick({}, {{"cond", 1}}) == "next");
  CHECK(when->may_start());
  CHECK_FALSE(when->restarts());  // by default a running path goes on
}

TEST_CASE("step/when options: on a fall (a key released), and restarting its path") {
  auto when = make("step/when", {{"on", 1}, {"running", 1}});  // fall, restart (by index)
  CHECK(when->restarts());
  CHECK(when->tick({}, {{"cond", 1}}).empty());  // pressed: nothing yet
  CHECK(when->tick({}, {{"cond", 1}}).empty());
  CHECK(when->tick({}, {{"cond", 0}}) == "next");  // released
  CHECK(when->tick({}, {{"cond", 0}}).empty());
  const auto& on = builtins().find("step/when")->def.params.at(0);
  CHECK(on.options == std::vector<std::string>{"rise", "fall"});
}

TEST_CASE("step/signal_step turns a 0/1 signal into a step wire") {
  auto on_signal = make("step/signal_step");
  CHECK(on_signal->may_start());
  CHECK(on_signal->tick({}, {}).empty());  // unwired: never
  CHECK(on_signal->tick({}, {{"signal", 1}}) == "next");
  CHECK(on_signal->tick({}, {{"signal", 1}}).empty());  // held: no second path
  CHECK(on_signal->tick({}, {{"signal", 0}}).empty());
  CHECK(on_signal->tick({}, {{"signal", 1}}) == "next");
  const auto& on = builtins().find("step/signal_step")->def.params.at(0);
  CHECK(on.id == "on");
  CHECK(on.options == std::vector<std::string>{"rise", "fall"});
  auto falling = make("step/signal_step", {{"on", 1}});  // fall (by index)
  CHECK(falling->tick({}, {{"signal", 1}}).empty());
  CHECK(falling->tick({}, {{"signal", 0}}) == "next");
}

TEST_CASE("step/while loops while its condition holds") {
  auto loop = make("step/while");
  CHECK(loop->tick({}, {{"cond", 1}}) == "loop");
  CHECK(loop->tick({}, {{"cond", 0}}) == "done");
  CHECK(loop->tick({}, {}) == "done");  // unwired reads as 0
}

TEST_CASE("step/say writes its text and value to the Console") {
  std::vector<std::string> lines;
  const LogFn log = [&](std::string_view, std::string_view, std::string_view text) { lines.emplace_back(text); };
  const Context ctx{.log_fn = &log};
  auto said = [&](const Values& in) {
    lines.clear();
    auto say = make("step/say");
    say->enter(ctx, in);
    CHECK(say->tick(ctx, in) == "next");
    return lines.size() == 1 ? lines[0] : "";
  };
  CHECK(said({}) == "hello");
  CHECK(said({{"value", 2.5}}) == "hello 2.5");
  CHECK(said({{"value", "fold"}}) == "hello fold");
  CHECK(said({{"value", List{1, 2, 3}}}) == "hello [1, 2, 3]");
}

TEST_CASE("step/join lets the last of its paths on; the others end there") {
  auto join = make("step/join", {{"paths", 2}});
  CHECK(join->shared());
  join->enter({}, {});
  CHECK(join->tick({}, {}).empty());  // one path: waits
  join->enter({}, {});
  CHECK(join->tick({}, {}) == "next");    // the second goes on
  CHECK(join->tick({}, {}) == "joined");  // the first ends (nothing is wired to joined)
  join->enter({}, {});
  CHECK(join->tick({}, {}).empty());  // counts again
  CHECK_FALSE(make("step/wait")->shared());
}

TEST_CASE("step/wait stays for its seconds") {
  auto wait = make("step/wait", {{"seconds", 0.05}});
  Context ctx{.t = 1.0, .dt = 0.01, .keys_down = nullptr};
  wait->enter(ctx, {});
  int ticks = 0;
  while (wait->tick(ctx, {}).empty()) {
    ctx.t += ctx.dt;
    ++ticks;
    REQUIRE(ticks < 100);
  }
  CHECK(ticks == 5);
}

TEST_CASE("logic blocks are magic: they compute in a signal flow and branch on a path") {
  const auto catalog = load_descriptor("domain = \"l\"\nimport = [\"base\"]\n");
  auto step = [&](const char* type, const Values& in, const Config& text = {}) {
    Config t = text;
    t["form"] = "step";
    return catalog.find(type)->make_step({}, t)->tick({}, in);
  };
  auto value = [&](const char* type, const Values& in, const Config& text = {}) {
    return catalog.find(type)->make({}, text)->step({}, in);
  };
  CHECK(step("if", {{"cond", 1.0}}) == "yes");
  CHECK(step("if", {{"cond", 0.0}}) == "no");
  CHECK(step("if", {}) == "no");  // unwired reads as 0
  CHECK(get_text(value("if", {{"cond", 1.0}, {"a", "fold"}, {"b", "launch"}}), "y") == "fold");  // any value
  CHECK(get_or(value("if", {{"cond", 0.0}, {"a", 1.0}, {"b", 2.0}}), "y") == 2.0);
  CHECK(get_or(value("and", {{"a", 1.0}, {"b", 1.0}}), "y") == 1.0);
  CHECK(step("and", {{"a", 1.0}, {"b", 0.0}}) == "no");
  CHECK(step("or", {{"a", 0.0}, {"b", 1.0}}) == "yes");
  CHECK(get_or(value("not", {{"u", 0.0}}), "y") == 1.0);
  CHECK(get_or(value("compare", {{"a", 2.0}, {"b", 3.0}}, {{"op", "<"}}), "y") == 1.0);
  CHECK(step("compare", {{"a", 2.0}, {"b", 3.0}}, {{"op", ">="}}) == "no");
  const auto& form = catalog.find("if")->def.params.at(0);
  CHECK(form.id == "form");
  CHECK(form.options == std::vector<std::string>{"signal", "step"});
}

TEST_CASE("step/stop ends the run") {
  auto stop = make("step/stop");
  CHECK(stop->ends_run());
  CHECK(stop->tick({}, {}).empty());
  CHECK_FALSE(make("step/wait")->ends_run());
}

TEST_CASE("step/repeat loops its times, then is done and starts over; count is the lap") {
  auto repeat = make("step/repeat", {{"times", 2}});
  auto arrive = [&] {
    repeat->enter({}, {});
    return repeat->tick({}, {});
  };
  auto count = [&] { return get_or(repeat->outputs(), "count", -1); };
  CHECK(count() == 0);
  CHECK(arrive() == "loop");
  CHECK(count() == 1);
  CHECK(arrive() == "loop");
  CHECK(count() == 2);
  CHECK(arrive() == "done");
  CHECK(count() == 2);  // how many it went round
  CHECK(arrive() == "loop");  // a second pass counts again
  CHECK(count() == 1);
}

TEST_CASE("a catalog entry makes the right kind of instance") {
  const auto catalog = load_descriptor(R"(
[[blocks]]
use = "step/wait"
defaults = { seconds = 2 }

[[blocks]]
use = "math/gain"
)");
  CHECK(catalog.find("wait")->make_step() != nullptr);
  CHECK_THROWS_AS(catalog.find("wait")->make(), std::logic_error);
  CHECK_THROWS_AS(catalog.find("gain")->make_step(), std::logic_error);
}

TEST_CASE("Timed: evenly over its seconds, or following its curve; ends at 1") {
  struct Slider final : paperboat::toolkit::Timed {
    using Timed::Timed;
    void advance(const Context&, double from, double to) override {
      CHECK(from == at);
      at = to;
    }
    double at = 0;
  };
  const Context ctx{.t = 0, .dt = 0.1};
  auto frames = [&](const Values& in) {
    Slider s(1.0);
    s.enter(ctx, in);
    std::vector<double> seen;
    for (int i = 0; i < 20; ++i) {
      const auto out = s.tick(ctx, in);
      seen.push_back(s.at);
      if (out == "next") break;
    }
    CHECK(get_or(s.outputs(), "progress") == Catch::Approx(1.0));
    return seen;
  };
  // A steady 0.5: at 0 first, then 0.5 the whole time, then 1 at the last frame.
  const auto steady = frames({{"curve", 0.5}});
  REQUIRE(steady.size() == 11);
  CHECK(steady.front() == 0.0);
  for (std::size_t i = 1; i + 1 < steady.size(); ++i) CHECK(steady[i] == 0.5);
  CHECK(steady.back() == 1.0);
  // Unwired: evenly, 0.1 a frame.
  const auto even = frames({});
  CHECK(even.front() == Catch::Approx(0.1));
  CHECK(even[4] == Catch::Approx(0.5));
  CHECK(even.back() == 1.0);
  // A curve out of range is clamped.
  CHECK(frames({{"curve", 7.0}})[3] == 1.0);
}
