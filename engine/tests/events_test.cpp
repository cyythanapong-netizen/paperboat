// Per-node events (#17, #7): metric, monitor.state, frame, and node.state queued / cached.

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/cache.hpp"
#include "paperboat/core/executor.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;
using Catch::Matchers::ContainsSubstring;

namespace {

// A solver in miniature: y = u, with a residual that halves each of its three iterations, and
// one point of its own x. A NaN point is dropped.
class Solver final : public tk::Block {
 public:
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    double residual = 1;
    for (int i = 0; i < 3; ++i, residual /= 2) ctx.metric("residual", residual);
    ctx.metric("residual", std::nan(""));
    ctx.metric("cost", 10, tk::get_or(in, "u"), "J");
    return {{"y", tk::get_or(in, "u")}};
  }
  bool stateless() const override { return true; }
};

// A Lamp-like monitor with an output: warn over 0.5, trip over 1. Says so every step.
class Alarm final : public tk::Block {
 public:
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    const double u = tk::get_or(in, "in");
    const auto state = u > 1 ? tk::MonitorState::Trip : u > 0.5 ? tk::MonitorState::Warn : tk::MonitorState::Ok;
    ctx.monitor_state(state, u);
    return {{"y", state == tk::MonitorState::Ok ? 0.0 : 1.0}};
  }
};

// Draws a pendulum each step: a rod and a bob at angle u, over a 2 × 2 field.
class Picture final : public tk::Block {
 public:
  explicit Picture(const tk::Values& p) : bad_(tk::get_or(p, "bad") != 0) {}
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    const double a = tk::get_or(in, "u");
    tk::Frame frame{.view = std::array<double, 4>{-1, -1, 1, 1}};
    frame.shapes.push_back({.t = "line", .p = {{0, 0}, {std::sin(a), -std::cos(a)}}});
    frame.shapes.push_back({.t = "circle", .p = {{std::sin(a), -std::cos(a)}}, .r = 0.1, .fill = "signal"});
    frame.field = tk::FrameField{.w = 2, .h = 2, .v = {0, 1, std::nan(""), 3}};
    if (bad_) frame.shapes.front().stroke = "#ff0000";  // a colour, not a token
    ctx.frame(frame);
    return {{"y", a}};
  }
  bool stateless() const override { return true; }

 private:
  bool bad_;
};

const tk::Catalog& catalog() {
  static const tk::Registry registry = [] {
    tk::Registry r = rig::registry();
    r.add(tk::define_block<Solver>("rig", {.type = "solver", .label = "Solver", .inputs = {{"u"}}, .outputs = {{"y"}}}));
    r.add(tk::define_block<Alarm>(
        "rig", {.type = "alarm", .label = "Alarm", .kind = tk::Kind::Monitor, .inputs = {{"in"}}, .outputs = {{"y"}}}));
    r.add(tk::define_block<Picture>(
        "rig", {.type = "picture", .label = "Picture", .inputs = {{"u"}}, .outputs = {{"y"}}, .params = {{.id = "bad", .default_value = 0}}}));
    return r;
  }();
  static const tk::Catalog c = tk::load_descriptor(R"(domain = "rig"
import = ["base", "math", "signal", "dashboard"]
)", registry);
  return c;
}

json node(const std::string& id, const std::string& type, json params = json::object()) {
  return {{"id", id}, {"type", type}, {"params", std::move(params)}};
}
json wire(const std::string& from, const std::string& out, const std::string& to, const std::string& in) {
  return {{"from", {from, out}}, {"to", {to, in}}};
}

std::vector<json> run(const json& graph, core::RunSettings settings, core::RunCache* cache = nullptr) {
  settings.real_time = false;
  const core::Backend backend{.engine = "test", .catalog = &catalog(), .run = settings};
  std::vector<json> out;
  const auto status = core::run("r", graph, nullptr, backend, [&](const json& m) { out.push_back(m); }, nullptr, {}, nullptr,
                                nullptr, nullptr, nullptr, cache);
  REQUIRE(status == "done");
  return out;
}

std::vector<json> of_type(const std::vector<json>& messages, const std::string& type) {
  std::vector<json> out;
  std::ranges::copy_if(messages, std::back_inserter(out), [&](const json& m) { return m["type"] == type; });
  return out;
}

// Where the first message `match` says yes to is in `messages`.
template <class Match>
std::ptrdiff_t index_of(const std::vector<json>& messages, Match match) {
  return std::ranges::find_if(messages, match) - messages.begin();
}

const core::RunSettings once{.mode = core::RunSettings::Mode::Once};

}  // namespace

TEST_CASE("metric: a series against its own x, sent while the block runs, and again when it is reused") {
  const json graph = {{"nodes", {node("a", "constant", {{"value", 4}}), node("s", "solver")}}, {"edges", {wire("a", "y", "s", "u")}}};
  core::RunCache cache;
  const auto first = run(graph, once, &cache);
  const auto metrics = of_type(first, "metric");
  REQUIRE(metrics.size() == 4);  // three residuals (the NaN dropped) and a cost
  for (std::size_t i = 0; i < 3; ++i)
    CHECK(metrics[i] == json{{"type", "metric"}, {"node", "s"}, {"series", "residual"}, {"x", i}, {"y", std::pow(0.5, static_cast<double>(i))}});
  CHECK(metrics[3] == json{{"type", "metric"}, {"node", "s"}, {"series", "cost"}, {"x", 10.0}, {"y", 4.0}, {"unit", "J"}});
  // Between the block's running and its done.
  const auto running = index_of(first, [](const json& m) { return m["type"] == "node.state" && m["node"] == "s" && m["state"] == "running"; });
  const auto done = index_of(first, [](const json& m) { return m["type"] == "node.state" && m["node"] == "s" && m["state"] == "done"; });
  const auto metric = index_of(first, [](const json& m) { return m["type"] == "metric"; });
  CHECK(running < metric);
  CHECK(metric < done);

  // Reused: cached, never running, and the same points sent again.
  const auto second = run(graph, once, &cache);
  CHECK(of_type(second, "metric") == metrics);
  std::vector<std::string> states;
  for (const auto& m : of_type(second, "node.state"))
    if (m["node"] == "s") states.push_back(m["state"]);
  CHECK(states == std::vector<std::string>{"queued", "cached"});
}

TEST_CASE("monitor.state: sent when it changes, with the run time and the value that changed it") {
  // clock → alarm, 0 to 1.5 s: warn past 0.5, trip past 1.
  const json graph = {{"nodes", {node("c", "clock"), node("lamp", "alarm")}}, {"edges", {wire("c", "t", "lamp", "in")}}};
  const auto out = run(graph, {.dt = 0.1, .duration = 1.5});
  const auto states = of_type(out, "monitor.state");
  REQUIRE(states.size() == 2);  // never ok first: every monitor starts there
  CHECK(states[0]["node"] == "lamp");
  CHECK(states[0]["state"] == "warn");
  CHECK(states[0]["t"] == 0.6);
  CHECK(std::abs(states[0]["v"].get<double>() - 0.6) < 1e-9);
  CHECK(states[1]["state"] == "trip");
  CHECK(states[1]["t"] == 1.1);
  CHECK(!states[0].contains("port"));
  CHECK(index_of(out, [](const json& m) { return m["type"] == "monitor.state"; }) <
        index_of(out, [](const json& m) { return m["type"] == "run.finished"; }));
}

TEST_CASE("frame: a live picture, at most ~20 a second per block, and always the last") {
  const json graph = {{"nodes", {node("c", "clock"), node("p", "picture")}}, {"edges", {wire("c", "t", "p", "u")}}};
  // 101 steps in a few milliseconds: the first frame, then the last one at the end of the run.
  const auto out = run(graph, {.dt = 0.01, .duration = 1});
  const auto frames = of_type(out, "frame");
  REQUIRE(frames.size() >= 2);
  CHECK(frames.size() < 10);
  CHECK(frames.front()["t"] == 0.0);
  CHECK(frames.back()["t"] == 1.0);
  const auto& last = frames.back();
  CHECK(last["node"] == "p");
  CHECK(last["view"] == json{-1, -1, 1, 1});
  REQUIRE(last["shapes"].size() == 2);
  CHECK(last["shapes"][0]["t"] == "line");
  CHECK(last["shapes"][0]["p"].size() == 2);
  CHECK(!last["shapes"][0].contains("stroke"));
  CHECK(last["shapes"][1] == json{{"t", "circle"}, {"p", {{std::sin(1.0), -std::cos(1.0)}}}, {"r", 0.1}, {"fill", "signal"}});
  CHECK(last["field"] == json{{"w", 2}, {"h", 2}, {"v", {0.0, 1.0, nullptr, 3.0}}});  // NaN: no value
  CHECK(index_of(out, [](const json& m) { return m["type"] == "frame" && m["t"] == 1.0; }) <
        index_of(out, [](const json& m) { return m["type"] == "run.finished"; }));

  SECTION("a run-once sends a block's last picture before its done, and again when it is reused") {
    const json once_graph = {{"nodes", {node("a", "constant", {{"value", 0.5}}), node("p", "picture")}}, {"edges", {wire("a", "y", "p", "u")}}};
    core::RunCache cache;
    const auto first = run(once_graph, once, &cache);
    REQUIRE(of_type(first, "frame").size() == 1);
    CHECK(index_of(first, [](const json& m) { return m["type"] == "frame"; }) <
          index_of(first, [](const json& m) { return m["type"] == "node.state" && m["node"] == "p" && m["state"] == "done"; }));
    const auto second = run(once_graph, once, &cache);
    CHECK(of_type(second, "frame") == of_type(first, "frame"));
  }
  SECTION("a frame it can't send warns once and is dropped") {
    const json bad = {{"nodes", {node("c", "clock"), node("p", "picture", {{"bad", 1}})}}, {"edges", {wire("c", "t", "p", "u")}}};
    const auto sent = run(bad, {.dt = 0.1, .duration = 1});
    CHECK(of_type(sent, "frame").empty());
    std::vector<json> warned;
    for (const auto& m : of_type(sent, "log"))
      if (m["text"].get<std::string>().starts_with("frame not shown")) warned.push_back(m);
    REQUIRE(warned.size() == 1);
    CHECK(warned[0]["node"] == "p");
    CHECK_THAT(warned[0]["text"].get<std::string>(), ContainsSubstring("no stroke \"#ff0000\""));
  }
}

TEST_CASE("node.state: a run-once queues every block before the first runs; a clock run queues none") {
  const json graph = {{"nodes", {node("a", "constant", {{"value", 1}}), node("s", "solver"), node("show", "display")}},
                      {"edges", {wire("a", "y", "s", "u"), wire("s", "y", "show", "in")}}};
  std::vector<std::string> states;
  for (const auto& m : of_type(run(graph, once), "node.state")) states.push_back(m["node"].get<std::string>() + ":" + m["state"].get<std::string>());
  CHECK(states == std::vector<std::string>{"a:queued", "s:queued", "show:queued", "a:running", "a:done", "s:running", "s:done",
                                           "show:running", "show:done"});
  CHECK(of_type(run(graph, {.dt = 0.1, .duration = 0.2}), "node.state").empty());
}
