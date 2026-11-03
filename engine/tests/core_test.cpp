// engine/core without a network: graphs, runs and sessions, with events collected.

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

const tk::Catalog& cart_pole_catalog() {
  static const tk::Catalog catalog = tk::load_descriptor_file(PAPERBOAT_TEST_DATA "/cart-pole.toml");
  return catalog;
}

// The descriptor's starters ([graph] and [station] in descriptor.toml).
json starter_graph() { return json::parse(cart_pole_catalog().graph); }
json starter_station() { return json::parse(cart_pole_catalog().station); }

struct Collected {
  std::vector<json> messages;
  core::Emit emit() {
    return [this](const json& m) { messages.push_back(m); };
  }
  double last_signal(const std::string& node) const {
    double v = 0;
    for (const auto& m : messages)
      if (m["type"] == "signal" && m["node"] == node) v = m["v"];
    return v;
  }
};

}  // namespace

TEST_CASE("the cart-pole starter run matches the reference result") {
  Collected out;
  const core::Backend backend{.engine = "test", .catalog = &cart_pole_catalog(), .run = {.real_time = false}};
  const auto status = core::run("run-1", starter_graph(), starter_station(), backend, out.emit(), nullptr, std::stop_token{});
  CHECK(status == "done");
  CHECK(out.messages.front()["type"] == "run.started");
  CHECK(out.messages.back()["type"] == "run.finished");
  // The same numbers every version of this example has produced.
  CHECK_THAT(out.last_signal("graph-theta"), WithinAbs(3.780e-07, 1e-10));
  CHECK_THAT(out.last_signal("graph-x"), WithinAbs(6.415e-07, 1e-10));
}

TEST_CASE("a wire into a linked block is not a cycle; a missing station object fails") {
  auto station = core::make_station(starter_station(), cart_pole_catalog());
  const auto g = core::prepare_graph(starter_graph(), cart_pole_catalog(), station);
  CHECK(g.order.front() == "cart-pole-1");  // read before anything uses it

  core::Station empty;
  CHECK_THROWS_WITH(core::prepare_graph(starter_graph(), cart_pole_catalog(), empty),
                    ContainsSubstring("isn't there"));

  const json loop = {{"nodes", {{{"id", "a"}, {"type", "gain"}}, {{"id", "b"}, {"type", "gain"}}}},
                     {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}}, {{"from", {"b", "y"}}, {"to", {"a", "u"}}}}}};
  CHECK_THROWS_WITH(core::prepare_graph(loop, cart_pole_catalog(), station), ContainsSubstring("cycle"));
}

TEST_CASE("a session sends the handshake, checks station edits and runs") {
  static const core::Backend backend{.engine = "test",
                                     .catalog = &cart_pole_catalog(),
                                     .run = {.dt = 0.01, .duration = 0.5, .emit_every = 10, .real_time = false}};
  std::vector<json> sent;
  std::mutex mutex;
  {
    core::Session session(backend, [&](const std::string& text) {
      std::lock_guard lock(mutex);
      sent.push_back(json::parse(text));
    });
    std::vector<std::string> types;
    for (const auto& m : sent) types.push_back(m["type"]);
    CHECK(types == std::vector<std::string>{"hello", "catalog", "scene", "graph.load", "station.load"});

    session.on_message(R"({"type": "station.set", "station": {"objects": [{"id": "x", "type": "nope"}]}})");
    CHECK(sent.back()["type"] == "log");
    CHECK(sent.back()["level"] == "error");

    session.on_message(R"({"type": "graph.run", "graph": )" + starter_graph().dump() + "}");
    // Wait for it: closing a session cancels its run.
    for (int i = 0; i < 500; ++i) {
      {
        std::lock_guard lock(mutex);
        if (sent.back()["type"] == "run.finished") break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  CHECK(sent.back()["type"] == "run.finished");
  CHECK(sent.back()["status"] == "done");
}

// --- Not a simulation: a Step flow, a run that evaluates once, and a domain's own blocks
// sharing its state. None of them needs a station or physics.

namespace {

const tk::Catalog& catalog_of(const char* toml) {
  static std::map<std::string, tk::Catalog> cache;
  auto [it, fresh] = cache.try_emplace(toml);
  if (fresh) it->second = tk::load_descriptor(toml);
  return it->second;
}

std::vector<std::string> logs(const Collected& out) {
  std::vector<std::string> text;
  for (const auto& m : out.messages)
    if (m["type"] == "log") text.push_back(m["text"]);
  return text;
}

}  // namespace

TEST_CASE("a Step flow runs in the core: paths, a When on a key, Stop") {
  const auto& catalog = catalog_of(R"(
domain = "steps"
import = ["step", "base", "signal", "dashboard"]
[graph]
nodes = [
  { id = "start", type = "start" }, { id = "wait", type = "wait", params = { seconds = 0.2 } },
  { id = "key", type = "key" }, { id = "when", type = "when" }, { id = "stop", type = "stop" },
  { id = "graph", type = "graph" }, { id = "clock", type = "clock" },
]
edges = [
  { from = ["start", "next"], to = ["wait", "in"] },
  { from = ["key", "pressed"], to = ["when", "cond"] },
  { from = ["when", "next"], to = ["stop", "in"] },
  { from = ["clock", "t"], to = ["graph", "in"] },
]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 5, .real_time = false}};
  const json graph = json::parse(catalog.graph);

  SECTION("without the key, the path ends and the When listens until the limit") {
    Collected out;
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
    std::vector<std::string> states;
    for (const auto& m : out.messages)
      if (m["type"] == "node.state") states.push_back(m["node"].get<std::string>() + ":" + m["state"].get<std::string>());
    CHECK(states == std::vector<std::string>{"wait:running", "wait:done"});
    CHECK(logs(out).back() == "stopped after 5 s");
  }
  SECTION("pressing the key starts the When's path, which reaches Stop") {
    Collected out;
    int calls = 0;
    core::KeysNow keys = [&] { return ++calls > 30 ? tk::KeySet{"Space"} : tk::KeySet{}; };
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), keys, {}) == "done");
    CHECK(logs(out).back() == "stopped by stop at t = 0.30 s");
  }
}

namespace words {

// The domain's shared state: a list of words, nothing physical.
struct Words final : tk::DomainState {
  std::vector<std::string> words{"fold", "launch", "watch"};
  double total = 0;
  std::optional<std::string> summary() const override { return "total " + std::to_string(static_cast<int>(total)); }
};
struct Lengths final : tk::Block {
  tk::Values step(const tk::Context& ctx, const tk::Values&) override {
    tk::List out;
    for (const auto& w : static_cast<Words*>(ctx.state)->words) out.push_back(static_cast<double>(w.size()));
    ctx.log("measured " + std::to_string(out.size()) + " words");
    return {{"y", out}};
  }
};
struct Total final : tk::Block {
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    double sum = 0;
    if (const auto* list = tk::get_if<tk::List>(in, "u"))
      for (double v : *list) sum += v;
    static_cast<Words*>(ctx.state)->total = sum;
    return {{"y", sum}};
  }
};
struct Boom final : tk::Block {
  tk::Values step(const tk::Context&, const tk::Values&) override { throw std::runtime_error("boom"); }
};
// A step that takes a second and reports how far it is; it moves `at` toward its `curve`.
struct Slide final : tk::Step {
  void enter(const tk::Context&, const tk::Values&) override { done = 0; }
  std::string tick(const tk::Context& ctx, const tk::Values& in) override {
    done += ctx.dt;
    seen = tk::get_or(in, "curve", -1);
    return done >= 1 - ctx.dt / 2 ? "next" : "";
  }
  tk::Values outputs() const override { return {{"progress", done}}; }
  double done = 0, seen = -1;
};
struct Shown final : tk::Block {  // a monitor that shows its input as is
  tk::Values step(const tk::Context&, const tk::Values&) override { return {}; }
};

const tk::Registry& registry() {
  static const tk::Registry r = [] {
    tk::Registry out = tk::builtins();
    out.add(tk::define_block<Lengths>("words", {.type = "lengths", .label = "Lengths", .outputs = {{"y", "numbers"}}}));
    out.add(tk::define_block<Total>("words", {.type = "total", .label = "Total", .inputs = {{"u", "numbers"}}, .outputs = {{"y"}}}));
    out.add(tk::define_block<Boom>("words", {.type = "boom", .label = "Boom", .outputs = {{"y"}}}));
    out.add(tk::define_block<Slide>("words", {.type = "slide", .label = "Slide", .inputs = {{"in", "step"}, {"curve"}},
                                              .outputs = {{"next", "step"}, {"progress"}}}));
    out.add(tk::define_block<Shown>("words", {.type = "graph", .label = "Graph", .kind = tk::Kind::Monitor,
                                              .inputs = {{"in", "numbers"}}, .instrument = "graph"}));
    return out;
  }();
  return r;
}

}  // namespace words

TEST_CASE("a run that evaluates once, with lists on the wires and a domain's own blocks") {
  static const tk::Catalog catalog = tk::load_descriptor(R"(
domain = "words"
[graph]
nodes = [{ id = "l", type = "lengths" }, { id = "t", type = "total" }, { id = "g", type = "graph" }]
edges = [{ from = ["l", "y"], to = ["t", "u"] }, { from = ["l", "y"], to = ["g", "in"] }]
)", words::registry());

  const core::Backend backend{
      .engine = "test",
      .catalog = &catalog,
      .state = [] { return std::make_unique<words::Words>(); },
      .view = nullptr,
      .run = {.mode = core::RunSettings::Mode::Once, .real_time = false},
  };
  Collected out;
  CHECK(core::run("r", json::parse(catalog.graph), nullptr, backend, out.emit(), nullptr, {}) == "done");
  std::vector<double> plotted;
  for (const auto& m : out.messages)
    if (m["type"] == "signal") plotted.push_back(m["v"]);
  CHECK(plotted == std::vector<double>{4, 6, 5});  // the list, by index
  const auto text = logs(out);
  CHECK(std::ranges::find(text, "total 15") != text.end());
  // What a block logs reaches the Console tagged with its id.
  CHECK(std::ranges::any_of(out.messages, [](const json& m) {
    return m["type"] == "log" && m.value("node", "") == "l" && m["text"] == "measured 3 words";
  }));
}

TEST_CASE("a run names the block at fault: an unknown type, or one that throws") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"words\"\n", words::registry());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .state = [] { return std::make_unique<words::Words>(); },
                              .view = nullptr, .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  auto failed_node = [](const Collected& out) {
    for (const auto& m : out.messages)
      if (m["type"] == "node.state" && m["state"] == "failed") return m["node"].get<std::string>();
    return std::string();
  };

  Collected lost;
  const json old = {{"nodes", {{{"id", "w-1"}, {"type", "wordcount"}}}}, {"edges", json::array()}};
  CHECK(core::run("r", old, nullptr, backend, lost.emit(), nullptr, {}) == "failed");
  CHECK(failed_node(lost) == "w-1");
  CHECK_THAT(logs(lost).back(), ContainsSubstring("unknown type \"wordcount\""));

  static const tk::Catalog signals = tk::load_descriptor("domain = \"f\"\nimport = [\"signal\", \"dashboard\"]\n");
  const core::Backend fb{.engine = "test", .catalog = &signals, .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  Collected bad_f;
  const json typo = {{"nodes", {{{"id", "f-1"}, {"type", "function"}, {"params", {{"f", "sinn(t)"}}}}}}, {"edges", json::array()}};
  CHECK(core::run("r", typo, nullptr, fb, bad_f.emit(), nullptr, {}) == "failed");
  CHECK(failed_node(bad_f) == "f-1");
  CHECK_THAT(logs(bad_f).back(), ContainsSubstring("unknown function \"sinn\""));

  Collected boom;
  const json graph = {{"nodes", {{{"id", "l"}, {"type", "lengths"}}, {{"id", "b-1"}, {"type", "boom"}}}}, {"edges", json::array()}};
  CHECK(core::run("r", graph, nullptr, backend, boom.emit(), nullptr, {}) == "failed");
  CHECK(failed_node(boom) == "b-1");
  CHECK(logs(boom).back() == "b-1: boom");
}

TEST_CASE("story groups run as steps, in sequence, handing on their results") {
  const auto& catalog = catalog_of(R"(
domain = "stories"
import = ["step", "base", "signal", "math", "dashboard"]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 5, .emit_every = 1, .real_time = false}};
  // Start → [simulate: ramp → gain, for 1 s] → [report: gain's result + 10, once].
  const json graph = {
      {"nodes",
       {{{"id", "start"}, {"type", "start"}},
        {{"id", "ramp"}, {"type", "ramp"}, {"params", {{"start", 0}, {"slope", 1}}}},
        {{"id", "double"}, {"type", "gain"}, {"params", {{"k", 2}}}},
        {{"id", "during"}, {"type", "graph"}},
        {{"id", "ten"}, {"type", "constant"}, {"params", {{"value", 10}}}},
        {{"id", "add"}, {"type", "sum"}},
        {{"id", "result"}, {"type", "display"}}}},
      {"edges",
       {{{"from", {"start", "next"}}, {"to", {"simulate", "in"}}},
        {{"from", {"simulate", "next"}}, {"to", {"report", "in"}}},
        {{"from", {"ramp", "y"}}, {"to", {"double", "u"}}},
        {{"from", {"double", "y"}}, {"to", {"during", "in"}}},
        {{"from", {"double", "y"}}, {"to", {"add", "a"}}},
        {{"from", {"ten", "y"}}, {"to", {"add", "b"}}},
        {{"from", {"add", "y"}}, {"to", {"result", "in"}}}}},
      {"groups",
       {{{"id", "simulate"}, {"kind", "story"}, {"nodes", {"ramp", "double", "during"}}, {"params", {{"seconds", 1}}}},
        {{"id", "report"}, {"kind", "story"}, {"nodes", {"ten", "add", "result"}}},
        {{"id", "tidy"}, {"kind", "visual"}, {"nodes", {"start"}}}}}};

  Collected out;
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  CHECK_THAT(out.last_signal("during"), WithinAbs(1.98, 1e-9));  // the last pass, t = 0.99 s
  CHECK_THAT(out.last_signal("result"), WithinAbs(11.98, 1e-9));  // report read simulate's result
  double first_result = -1;
  for (const auto& m : out.messages)
    if (m["type"] == "signal" && m["node"] == "result" && first_result < 0) first_result = m["t"];
  CHECK(first_result >= 0.99);  // report's blocks didn't run before its turn
  std::vector<std::string> states;
  for (const auto& m : out.messages)
    if (m["type"] == "node.state") states.push_back(m["node"].get<std::string>() + ":" + m["state"].get<std::string>());
  CHECK(states == std::vector<std::string>{"simulate:running", "simulate:done", "report:running", "report:done"});

  core::Station none;
  json bad = graph;
  bad["groups"][0]["nodes"].push_back("start");
  CHECK_THROWS_WITH(core::prepare_graph(bad, catalog, none), ContainsSubstring("can't be inside a story"));
  bad = graph;
  bad["groups"][1]["nodes"].push_back("nobody");
  CHECK_THROWS_WITH(core::prepare_graph(bad, catalog, none), ContainsSubstring("isn't a block of the graph"));
}

TEST_CASE("a step's signal output feeds signal blocks, and back into it, a clock step later") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"words\"\nimport = [\"step\", \"math\", \"dashboard\"]\n",
                                                         words::registry());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.1, .duration = 5, .emit_every = 1, .real_time = false}};
  const json graph = {
      {"nodes", {{{"id", "start"}, {"type", "start"}}, {{"id", "s"}, {"type", "slide"}},
                 {{"id", "half"}, {"type", "gain"}, {"params", {{"k", 0.5}}}}, {{"id", "g"}, {"type", "display"}}}},
      {"edges", {{{"from", {"start", "next"}}, {"to", {"s", "in"}}},
                 {{"from", {"s", "progress"}}, {"to", {"half", "u"}}},
                 {{"from", {"half", "y"}}, {"to", {"s", "curve"}}},  // back into the step: not a cycle
                 {{"from", {"half", "y"}}, {"to", {"g", "in"}}}}}};
  Collected out;
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  // The last pass saw the progress after the clock step before the last: 0.9, halved.
  CHECK_THAT(out.last_signal("g"), WithinAbs(0.45, 1e-9));
}
