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
  CHECK_FALSE(has_step_ports(builtins().find("control/pid")->def));
  // If reads its condition from a signal port.
  const auto& in = builtins().find("step/if")->def.inputs;
  REQUIRE(in.size() == 2);
  CHECK(in[1].id == "cond");
  CHECK(in[1].type == "signal");
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

TEST_CASE("step/if branches on a positive condition") {
  auto branch = make("step/if");
  CHECK(branch->tick({}, {{"cond", 1.0}}) == "yes");
  CHECK(branch->tick({}, {{"cond", 0.0}}) == "no");
  CHECK(branch->tick({}, {}) == "no");  // unwired reads as 0
}

TEST_CASE("step/stop ends the run") {
  auto stop = make("step/stop");
  CHECK(stop->ends_run());
  CHECK(stop->tick({}, {}).empty());
  CHECK_FALSE(make("step/wait")->ends_run());
}

TEST_CASE("step/repeat loops its times, then is done and starts over") {
  auto repeat = make("step/repeat", {{"times", 2}});
  auto arrive = [&] {
    repeat->enter({}, {});
    return repeat->tick({}, {});
  };
  CHECK(arrive() == "loop");
  CHECK(arrive() == "loop");
  CHECK(arrive() == "done");
  CHECK(arrive() == "loop");  // a second pass counts again
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
