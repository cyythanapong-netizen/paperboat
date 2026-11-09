// engine/core without a network: graphs, runs and sessions, with events collected.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/executor.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

// The test domain's starters ([graph] and [station] in data/rig.toml).
json starter_graph() { return json::parse(rig::catalog().graph); }
json starter_station() { return json::parse(rig::catalog().station); }

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

class WrongVector final : public tk::Block {
 public:
  tk::Values step(const tk::Context&, const tk::Values&) override { return {{"state", tk::Vector{1, 2}}}; }
};

const tk::Registry& vector_registry() {
  static const tk::Registry registry = [] {
    tk::Registry r = tk::builtins();
    r.add(tk::define_block<WrongVector>("vectors", {.type = "wrong-vector",
                                                       .label = "Wrong vector",
                                                       .outputs = {tk::vector_port("state", 3)}}));
    return r;
  }();
  return registry;
}

const tk::Catalog& vector_catalog() {
  static const tk::Catalog catalog = tk::load_descriptor(R"(
domain = "vectors"
[graph]
nodes = [{ id = "bad", type = "wrong-vector" }]
edges = []
)", vector_registry());
  return catalog;
}

}  // namespace

TEST_CASE("the starter run matches the reference result") {
  Collected out;
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.real_time = false}};
  const auto status = core::run("run-1", starter_graph(), starter_station(), backend, out.emit(), nullptr, std::stop_token{});
  CHECK(status == "done");
  CHECK(out.messages.front()["type"] == "run.started");
  CHECK(out.messages.back()["type"] == "run.finished");
  // Frozen numbers. A PD loop on a 1 kg slider (poles -1.5 ± 1.32i): after 10 s it is
  // within e^-15 ≈ 3e-7 of rest.
  CHECK_THAT(out.last_signal("graph-x"), WithinAbs(3.597e-07, 1e-10));
  CHECK_THAT(out.last_signal("graph-v"), WithinAbs(-4.420e-07, 1e-10));
}

TEST_CASE("a vector output must match its declared dimension") {
  const core::Backend backend{.engine = "test", .catalog = &vector_catalog(), .run = {.real_time = false}};
  const json catalog = json::parse(tk::catalog_message(vector_catalog()));
  const auto definition = std::ranges::find_if(catalog.at("blocks"), [](const json& block) {
    return block.at("type") == "wrong-vector";
  });
  REQUIRE(definition != catalog.at("blocks").end());
  CHECK(definition->at("outputs").at(0).at("type") == "vector");
  CHECK(definition->at("outputs").at(0).at("dimension") == 3);

  Collected out;
  const auto graph = json::parse(vector_catalog().graph);
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "failed");
  CHECK(std::ranges::any_of(out.messages, [](const json& message) {
    return message.value("type", "") == "node.state" && message.value("node", "") == "bad" &&
           message.value("state", "") == "failed";
  }));
  CHECK(std::ranges::any_of(out.messages, [](const json& message) {
    return message.value("type", "") == "log" &&
           message.value("text", std::string{}).find("output bad.state has 2 values, expected 3") != std::string::npos;
  }));
}

TEST_CASE("a wire into a linked block is not a cycle; a missing station object fails") {
  auto station = core::make_station(starter_station(), rig::catalog());
  const auto g = core::prepare_graph(starter_graph(), rig::catalog(), station);
  CHECK(g.order.front() == "slider-1");  // read before anything uses it

  core::Station empty;
  CHECK_THROWS_WITH(core::prepare_graph(starter_graph(), rig::catalog(), empty),
                    ContainsSubstring("isn't there"));

  const json loop = {{"nodes", {{{"id", "a"}, {"type", "gain"}}, {{"id", "b"}, {"type", "gain"}}}},
                     {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}}, {{"from", {"b", "y"}}, {"to", {"a", "u"}}}}}};
  CHECK_THROWS_WITH(core::prepare_graph(loop, rig::catalog(), station), ContainsSubstring("cycle"));
}

TEST_CASE("a session sends the handshake, checks station edits and runs") {
  static const core::Backend backend{.engine = "test",
                                     .catalog = &rig::catalog(),
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

    // Variables (#36): each set is answered with what they come to, or why they don't hold.
    session.on_message(R"({"type": "variables.set", "variables": [{"name": "k", "type": "number", "value": 2},
                                                                     {"name": "k2", "type": "number", "expr": "k * k"}]})");
    auto last = [&](const char* type) {
      for (auto it = sent.rbegin(); it != sent.rend(); ++it)
        if ((*it)["type"] == type) return *it;
      return json();
    };
    CHECK(last("variables.values")["values"] == json{{"k", 2.0}, {"k2", 4.0}});
    session.on_message(R"({"type": "variables.set", "variables": [{"name": "k2", "type": "number", "expr": "kk * 2"}]})");
    CHECK(last("variables.values")["at"] == "k2");
    CHECK_THAT(last("variables.values")["error"].get<std::string>(), ContainsSubstring("unknown variable \"kk\""));
    session.on_message(R"({"type": "variables.set", "variables": []})");
    CHECK(last("variables.values")["values"] == json::object());

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

TEST_CASE("variables (#36): the same run with its numbers given by variables, and errors on the block") {
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.real_time = false}};
  json graph = starter_graph();
  json station = starter_station();
  for (auto& n : graph["nodes"])
    if (n["id"] == "pid-x") n["params"] = {{"kp", {{"var", "kp"}}}, {"ki", 0}, {"kd", {{"expr", "0.75 * kp"}}}};
  station["objects"][0]["props"]["x0"] = {{"var", "start"}};
  const json variables = json::array({{{"name", "kp"}, {"type", "number"}, {"value", 4}},
                                      {{"name", "start"}, {"type", "number"}, {"value", 1}, {"unit", "m"}}});
  SECTION("resolved, the run is the reference run") {
    Collected out;
    CHECK(core::run("r", graph, station, backend, out.emit(), nullptr, {}, nullptr, variables) == "done");
    CHECK_THAT(out.last_signal("graph-x"), WithinAbs(3.597e-07, 1e-10));
  }
  SECTION("an unknown variable fails the run, on the block that names it") {
    graph["nodes"][1]["params"]["kp"] = {{"var", "kq"}};
    Collected out;
    CHECK(core::run("r", graph, station, backend, out.emit(), nullptr, {}, nullptr, variables) == "failed");
    bool marked = false;
    for (const auto& m : out.messages)
      if (m["type"] == "log" && m["level"] == "error") {
        CHECK(m["node"] == "pid-x");
        CHECK_THAT(m["text"].get<std::string>(), ContainsSubstring("unknown variable \"kq\" (kp?)"));
        marked = true;
      }
    CHECK(marked);
  }
  SECTION("a bad list fails the run, naming the variable") {
    Collected out;
    const json loop = json::array({{{"name", "kp"}, {"type", "number"}, {"expr", "kp + 1"}}, variables[1]});
    CHECK(core::run("r", graph, station, backend, out.emit(), nullptr, {}, nullptr, loop) == "failed");
    CHECK_THAT(out.messages.at(out.messages.size() - 2)["text"].get<std::string>(), ContainsSubstring("variables loop: kp → kp"));
  }
}

TEST_CASE("a headless run takes --var for a default variable") {
  static const auto catalog = tk::load_descriptor(R"(domain = "v"
import = ["base", "dashboard"]
[[variables]]
name = "level"
value = 2
[[variables]]
name = "double"
expr = "2 * level"
[graph]
nodes = [{ id = "c", type = "constant", params = { value = { var = "double" } } }, { id = "show", type = "display" }]
edges = [{ from = ["c", "y"], to = ["show", "in"] }]
)");
  CHECK(core::with_vars(catalog, {{"level", "5"}})[0]["value"] == 5.0);
  CHECK(core::with_vars(catalog, {{"double", "7"}})[1]["value"] == 7.0);  // a formula set by hand
  CHECK(!core::with_vars(catalog, {{"double", "7"}})[1].contains("expr"));
  CHECK_THROWS_WITH(core::with_vars(catalog, {{"lvl", "5"}}), ContainsSubstring("no such variable (this backend has level, double)"));
  CHECK_THROWS_WITH(core::with_vars(catalog, {{"level", "high"}}), ContainsSubstring("a number, not \"high\""));
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  std::ostringstream out;
  CHECK(core::run_starter(backend, out, {{"level", "5"}}) == "done");
  CHECK_THAT(out.str(), ContainsSubstring(R"("node":"show")"));
  CHECK_THAT(out.str(), ContainsSubstring(R"("v":10.0)"));
  const char* argv[] = {"backend", "--run", "--var", "level=5"};
  const auto o = core::parse_options(4, const_cast<char**>(argv), 8765);
  CHECK(o.vars == std::vector<std::pair<std::string, std::string>>{{"level", "5"}});
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

TEST_CASE("real-time pacing warns when computation falls behind the wall clock") {
  const auto& catalog = catalog_of(R"(
domain = "lag"
import = ["step", "base", "signal"]
[graph]
nodes = [{ id = "start", type = "start" }, { id = "key", type = "key" }, { id = "when", type = "when" }]
edges = [{ from = ["key", "pressed"], to = ["when", "cond"] }]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 1, .real_time = true, .speed = 1000}};
  const json graph = json::parse(catalog.graph);
  Collected out;
  core::KeysNow slow = [] { std::this_thread::sleep_for(std::chrono::milliseconds(4)); return tk::KeySet{}; };  // ~0.4 s of work for 1 ms of wall budget
  core::run("r", graph, nullptr, backend, out.emit(), slow, {});
  bool warned = false;
  for (const auto& m : out.messages)
    if (m["type"] == "log" && m["level"] == "warn" && m["text"].get<std::string>().find("computation delay") != std::string::npos) warned = true;
  CHECK(warned);
}

TEST_CASE("a Step flow joins paths, says what it sees, loops while and restarts on a key") {
  const auto& catalog = catalog_of(R"(
domain = "steps2"
import = ["step", "base", "signal"]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 5, .real_time = false}};
  auto run = [&](const json& graph, core::KeysNow keys = nullptr) {
    Collected out;
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), keys, {}) == "done");
    return out;
  };
  auto node = [](const char* id, const char* type, json params = json::object()) {
    return json{{"id", id}, {"type", type}, {"params", std::move(params)}};
  };
  auto wire = [](const char* a, const char* ap, const char* b, const char* bp) {
    return json{{"from", {a, ap}}, {"to", {b, bp}}};
  };

  SECTION("Join waits for both paths, then one goes on") {
    const json graph{{"nodes", {node("start", "start"), node("one", "constant"), node("when", "when"),
                                node("a", "wait", {{"seconds", 0.1}}), node("b", "wait", {{"seconds", 0.3}}),
                                node("join", "join"), node("say", "say", {{"text", "both here"}}), node("stop", "stop")}},
                     {"edges", {wire("one", "y", "when", "cond"), wire("start", "next", "a", "in"),
                                wire("when", "next", "b", "in"), wire("a", "next", "join", "in"),
                                wire("b", "next", "join", "in"), wire("join", "next", "say", "in"),
                                wire("say", "next", "stop", "in")}}};
    const auto out = run(graph);
    const auto text = logs(out);
    CHECK(std::ranges::count(text, "both here") == 1);
    CHECK(text.back() == "stopped by stop at t = 0.30 s");
  }
  SECTION("Repeat's count reaches a Say in the same lap; While ends the loop") {
    const json graph{{"nodes", {node("start", "start"), node("repeat", "repeat", {{"times", 3.0}}),
                                node("say", "say", {{"text", "lap"}}), node("wait", "wait", {{"seconds", 0.05}}),
                                node("one", "constant"), node("again", "while"), node("stop", "stop")}},
                     {"edges", {wire("start", "next", "repeat", "in"), wire("repeat", "loop", "say", "in"),
                                wire("repeat", "count", "say", "value"), wire("say", "next", "wait", "in"),
                                wire("wait", "next", "repeat", "in"), wire("repeat", "done", "again", "in"),
                                wire("one", "y", "again", "cond"), wire("again", "loop", "stop", "in")}}};
    const auto text = logs(run(graph));
    std::vector<std::string> laps;
    std::ranges::copy_if(text, std::back_inserter(laps), [](const std::string& t) { return t.starts_with("lap"); });
    CHECK(laps == std::vector<std::string>{"lap 1", "lap 2", "lap 3"});
    CHECK(text.back() == "stopped by stop at t = 0.15 s");
  }
  SECTION("a When set to restart starts its running path over") {
    const json graph{{"nodes", {node("key", "key"), node("when", "when", {{"running", "restart"}}),
                                node("wait", "wait", {{"seconds", 0.5}}), node("stop", "stop")}},
                     {"edges", {wire("key", "pressed", "when", "cond"), wire("when", "next", "wait", "in"),
                                wire("wait", "next", "stop", "in")}}};
    int calls = 0;
    // Pressed at the first step, released, pressed again at 0.2 s: the wait starts over.
    core::KeysNow keys = [&] {
      ++calls;
      return calls <= 2 || (calls > 20 && calls <= 22) ? tk::KeySet{"Space"} : tk::KeySet{};
    };
    CHECK(logs(run(graph, keys)).back() == "stopped by stop at t = 0.70 s");
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
    const auto& words = static_cast<Words*>(ctx.state)->words;
    for (const auto& w : words) {
      out.push_back(static_cast<double>(w.size()));
      ctx.progress(static_cast<double>(out.size()) / static_cast<double>(words.size()));
    }
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
struct Count final : tk::Block {  // counts its own passes
  tk::Values step(const tk::Context&, const tk::Values&) override { return {{"y", ++n}}; }
  double n = 0;
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
    out.add(tk::define_block<Count>("words", {.type = "count", .label = "Count", .outputs = {{"y"}}}));
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
  // Block by block, each running then done; a block's progress, throttled but always ending at 1.
  std::vector<std::string> states;
  std::vector<double> progress;
  for (const auto& m : out.messages) {
    if (m["type"] == "node.state") states.push_back(m["node"].get<std::string>() + ":" + m["state"].get<std::string>());
    if (m["type"] == "node.progress" && m["node"] == "l") progress.push_back(m["fraction"]);
  }
  CHECK(states == std::vector<std::string>{"l:running", "l:done", "t:running", "t:done", "g:running", "g:done"});
  REQUIRE(progress.size() >= 2);
  CHECK_THAT(progress.front(), WithinAbs(1.0 / 3, 1e-9));
  CHECK(progress.back() == 1.0);
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

  static const tk::Catalog signals = tk::load_descriptor("domain = \"f\"\nimport = [\"signal\", \"base\", \"dashboard\"]\n");
  const core::Backend fb{.engine = "test", .catalog = &signals, .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  Collected bad_f;
  const json typo = {{"nodes", {{{"id", "f-1"}, {"type", "function"}, {"params", {{"f", "sinn(t)"}}}}}}, {"edges", json::array()}};
  CHECK(core::run("r", typo, nullptr, fb, bad_f.emit(), nullptr, {}) == "failed");
  CHECK(failed_node(bad_f) == "f-1");
  CHECK_THAT(logs(bad_f).back(), ContainsSubstring("unknown function \"sinn\""));

  Collected bad_reserved;
  const json reserved = {{"nodes", {{{"id", "f-1"}, {"type", "function"}, {"params", {{"f", "sin + 1"}}}}}}, {"edges", json::array()}};
  CHECK(core::run("r", reserved, nullptr, fb, bad_reserved.emit(), nullptr, {}) == "failed");
  CHECK(failed_node(bad_reserved) == "f-1");
  CHECK_THAT(logs(bad_reserved).back(), ContainsSubstring("unknown name \"sin\""));

  Collected wired_fn;
  const json wired_graph = {
      {"nodes",
       {{{"id", "c1"}, {"type", "constant"}, {"params", {{"value", 3.0}}}},
        {{"id", "c2"}, {"type", "constant"}, {"params", {{"value", 4.0}}}},
        {{"id", "fn"}, {"type", "function"}, {"params", {{"f", "2 * x + y"}}}},
        {{"id", "show"}, {"type", "display"}}}},
      {"edges",
       {{{"from", {"c1", "y"}}, {"to", {"fn", "x"}}},
        {{"from", {"c2", "y"}}, {"to", {"fn", "y"}}},
        {{"from", {"fn", "y"}}, {"to", {"show", "in"}}}}}};
  CHECK(core::run("r", wired_graph, nullptr, fb, wired_fn.emit(), nullptr, {}) == "done");
  CHECK(wired_fn.last_signal("show") == 10.0);

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

TEST_CASE("a story with reset starts its blocks afresh each time a path arrives") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"words\"\nimport = [\"step\", \"dashboard\"]\n",
                                                         words::registry());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.1, .duration = 5, .emit_every = 1, .real_time = false}};
  // Start → Repeat ×2 → [count, 0.3 s] → back to Repeat: the story runs twice, 3 passes each.
  auto graph = [](bool reset) {
    return json{
        {"nodes", {{{"id", "start"}, {"type", "start"}}, {{"id", "rep"}, {"type", "repeat"}, {"params", {{"times", 2}}}},
                   {{"id", "c"}, {"type", "count"}}, {{"id", "shown"}, {"type", "display"}}}},
        {"edges", {{{"from", {"start", "next"}}, {"to", {"rep", "in"}}},
                   {{"from", {"rep", "loop"}}, {"to", {"story", "in"}}},
                   {{"from", {"story", "next"}}, {"to", {"rep", "in"}}},
                   {{"from", {"c", "y"}}, {"to", {"shown", "in"}}}}},
        {"groups", {{{"id", "story"}, {"kind", "story"}, {"nodes", {"c", "shown"}},
                     {"params", {{"seconds", 0.3}, {"reset", reset}}}}}}};
  };
  Collected kept, fresh;
  CHECK(core::run("r", graph(false), nullptr, backend, kept.emit(), nullptr, {}) == "done");
  CHECK(core::run("r", graph(true), nullptr, backend, fresh.emit(), nullptr, {}) == "done");
  CHECK(kept.last_signal("shown") == 6.0);   // the same block, counting on
  CHECK(fresh.last_signal("shown") == 3.0);  // a fresh block the second time
}

TEST_CASE("a story ends when its until input says so, and drops what it releases") {
  const auto& catalog = catalog_of(R"(
domain = "until"
import = ["step", "base", "signal", "math", "dashboard"]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.1, .duration = 5, .emit_every = 1, .real_time = false}};
  // Start → [drive: ramp → push, estimate; until ramp > 0.45] → [after: 2 s].
  // push (released) drives something; estimate (held) is a result.
  auto graph = [](json until_from, double seconds) {
    json g = {
        {"nodes",
         {{{"id", "start"}, {"type", "start"}},
          {{"id", "ramp"}, {"type", "ramp"}, {"params", {{"start", 0}, {"slope", 1}}}},
          {{"id", "push"}, {"type", "gain"}, {"params", {{"k", 1}}}},
          {{"id", "estimate"}, {"type", "gain"}, {"params", {{"k", 2}}}},
          {{"id", "over"}, {"type", "threshold"}, {"params", {{"level", 0.45}}}},
          {{"id", "wait"}, {"type", "constant"}},
          {{"id", "force"}, {"type", "display"}},
          {{"id", "result"}, {"type", "display"}}}},
        {"edges",
         {{{"from", {"start", "next"}}, {"to", {"drive", "in"}}},
          {{"from", {"drive", "next"}}, {"to", {"after", "in"}}},
          {{"from", {"ramp", "y"}}, {"to", {"push", "u"}}},
          {{"from", {"ramp", "y"}}, {"to", {"estimate", "u"}}},
          {{"from", {"ramp", "y"}}, {"to", {"over", "u"}}},
          {{"from", {"push", "y"}}, {"to", {"force", "in"}}},
          {{"from", {"estimate", "y"}}, {"to", {"result", "in"}}}}},
        {"groups",
         {{{"id", "drive"}, {"kind", "story"}, {"nodes", {"ramp", "push", "estimate", "over"}},
           {"params", {{"seconds", seconds}}}, {"release", json::array({json::array({"push", "y"})})}},
          {{"id", "after"}, {"kind", "story"}, {"nodes", {"wait"}}, {"params", {{"seconds", 2}}}}}}};
    if (!until_from.is_null()) g["edges"].push_back({{"from", until_from}, {"to", {"drive", "until"}}});
    return g;
  };
  auto left_at = [](const Collected& out) {
    // When drive's path moved on: the time of the frame in which `after` started (its
    // node.state comes before that frame's signals).
    bool started = false;
    for (const auto& m : out.messages) {
      if (m["type"] == "node.state" && m["node"] == "after" && m["state"] == "running") started = true;
      if (started && m["type"] == "signal" && m["node"] == "force") return m["t"].get<double>();
    }
    return -1.0;
  };

  Collected out;
  CHECK(core::run("r", graph({"over", "y"}, 0), nullptr, backend, out.emit(), nullptr, {}) == "done");
  // ramp passes 0.45 on the pass at t = 0.5; `until` reads it a clock step later.
  CHECK_THAT(left_at(out), WithinAbs(0.6, 1e-9));
  CHECK(out.last_signal("force") == 0.0);                  // released: unwired once drive is over
  CHECK_THAT(out.last_signal("result"), WithinAbs(1.2, 1e-9));  // held: drive's last estimate

  // `seconds` is then a time limit: a condition that never comes still ends the story.
  Collected limited;
  CHECK(core::run("r", graph({"wait", "y"}, 0.3), nullptr, backend, limited.emit(), nullptr, {}) == "done");
  CHECK_THAT(left_at(limited), WithinAbs(0.2, 1e-9));

  // A release that isn't the story's output is refused, naming the story.
  core::Station none;
  json bad = graph(nullptr, 1);
  bad["groups"][0]["release"] = json::array({json::array({"wait", "y"})});
  CHECK_THROWS_WITH(core::prepare_graph(bad, catalog, none), ContainsSubstring("isn't one of its blocks"));
  bad["groups"][0]["release"] = json::array({json::array({"push", "nope"})});
  CHECK_THROWS_WITH(core::prepare_graph(bad, catalog, none), ContainsSubstring("which push doesn't have"));
}

TEST_CASE("a magic block takes the ports of its form in a run") {
  const auto& catalog = catalog_of(R"(
domain = "magic"
import = ["step", "base", "dashboard"]
)");
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.1, .duration = 2, .emit_every = 1, .real_time = false}};
  // Step form: Start → Compare(1 > 0) → yes → Stop. Signal form: If chooses 7 or 9.
  const json graph = {
      {"nodes", {{{"id", "start"}, {"type", "start"}},
                 {{"id", "check"}, {"type", "compare"}, {"params", {{"form", "step"}, {"op", ">"}}}},
                 {{"id", "one"}, {"type", "constant"}, {"params", {{"value", 1}}}},
                 {{"id", "stop"}, {"type", "stop"}},
                 {{"id", "seven"}, {"type", "constant"}, {"params", {{"value", 7}}}},
                 {{"id", "pick"}, {"type", "if"}},
                 {{"id", "shown"}, {"type", "display"}}}},
      {"edges", {{{"from", {"start", "next"}}, {"to", {"check", "in"}}},
                 {{"from", {"one", "y"}}, {"to", {"check", "a"}}},
                 {{"from", {"check", "yes"}}, {"to", {"stop", "in"}}},
                 {{"from", {"one", "y"}}, {"to", {"pick", "cond"}}},
                 {{"from", {"seven", "y"}}, {"to", {"pick", "a"}}},
                 {{"from", {"pick", "y"}}, {"to", {"shown", "in"}}}}}};
  Collected out;
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  CHECK_THAT(logs(out).back(), ContainsSubstring("stopped by stop"));
  CHECK(out.last_signal("shown") == 7.0);

  // A wire to a port its form doesn't have fails, naming the block.
  json wrong = graph;
  wrong["nodes"][1]["params"]["form"] = "signal";
  Collected bad;
  CHECK(core::run("r", wrong, nullptr, backend, bad.emit(), nullptr, {}) == "failed");
  CHECK_THAT(logs(bad).back(), ContainsSubstring("check (compare) has no input \"in\""));
}

TEST_CASE("a backend can bring its own executor, driving the same run") {
  // An executor that runs ten passes on a clock of its own, as an event-driven or
  // external-solver run style would, and reports nothing but its frames.
  class Ten final : public core::Executor {
   public:
    std::string execute(core::Run& run) override {
      for (int k = 0; k < 10; ++k) {
        if (run.cancelled()) return "cancelled";
        run.at_time(k * 0.5);
        run.frame(run.pass(), k / 9.0);
      }
      run.log("info", "ten passes");
      return "done";
    }
  };
  int asked = 0;
  core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.real_time = false}};
  backend.executor = [&asked](const core::Graph& g, const core::RunSettings&) -> std::unique_ptr<core::Executor> {
    ++asked;
    if (g.has_steps) return nullptr;  // not its kind of graph: the core's choice
    return std::make_unique<Ten>();
  };
  Collected out;
  CHECK(core::run("r", starter_graph(), starter_station(), backend, out.emit(), nullptr, {}) == "done");
  CHECK(asked == 1);
  const auto samples = std::ranges::count_if(out.messages, [](const json& m) { return m["type"] == "signal" && m["node"] == "graph-v"; });
  CHECK(samples == 10);
  CHECK(std::ranges::any_of(out.messages, [](const json& m) { return m["type"] == "log" && m["text"] == "ten passes"; }));
}

TEST_CASE("graph.run's settings override the backend's run settings") {
  const core::RunSettings base{.duration = 10, .real_time = false};
  const auto s = core::with_overrides(base, json{{"duration", 2.5}, {"dt", 0.05}, {"speed", 0}});
  CHECK(s.duration == 2.5);
  CHECK(s.dt == 0.05);
  CHECK(s.speed == 0);
  CHECK(s.mode == base.mode);  // the backend's to say
  CHECK(core::with_overrides(base, nullptr).duration == 10);
  CHECK(core::with_overrides(base, json{{"duration", 0}}).duration == 0);
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"duration", -1}}), ContainsSubstring("duration"));
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"dt", 0}}), ContainsSubstring("dt"));
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"duration", "long"}}), ContainsSubstring("duration"));
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"duration", 1e6}, {"dt", 1e-3}}), ContainsSubstring("10 million"));
  CHECK(core::settings_json(base)["mode"] == "clock");
  // hello.run names the solvers this engine has (#217): the UI offers no others.
  const json hello = core::settings_json(base);
  CHECK(std::ranges::find(hello["solvers"], "rk4") != hello["solvers"].end());
  CHECK(std::ranges::find(hello["solvers"], "discrete") != hello["solvers"].end());

  // The solver and its options (#196): given ones over the backend's, checked.
  const auto o = core::with_overrides(base, json{{"solver", "rk45"}, {"solver_options", {{"rtol", 1e-8}, {"max_step", 0.001}}}});
  CHECK(o.solver == "rk45");
  CHECK(o.solver_options.rtol == 1e-8);
  CHECK(o.solver_options.max_step == 0.001);
  CHECK(o.solver_options.atol == paperboat::toolkit::solve::Options{}.atol);
  CHECK(core::settings_json(o)["solver_options"]["max_step"] == 0.001);
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"solver_options", {{"tol", 1}}}}), ContainsSubstring("no solver option tol"));
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"solver_options", {{"sub_steps", 1.5}}}}), ContainsSubstring("whole number"));
  CHECK_THROWS_WITH(core::with_overrides(base, json{{"solver_options", {{"sub_steps", 0}}}}), ContainsSubstring("sub_steps"));

  // A run takes them: a shorter clock run ends sooner, a bad one fails before it starts.
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = base};
  Collected out;
  CHECK(core::run("r", starter_graph(), starter_station(), backend, out.emit(), nullptr, {}, json{{"duration", 0.5}}) == "done");
  double last_t = 0;
  for (const auto& m : out.messages)
    if (m["type"] == "signal") last_t = m["t"];
  CHECK_THAT(last_t, WithinAbs(0.5, 1e-9));
  Collected bad;
  CHECK(core::run("r", starter_graph(), starter_station(), backend, bad.emit(), nullptr, {}, json{{"dt", -1}}) == "failed");
  CHECK(bad.messages.back()["status"] == "failed");
  // A run's "auto" solvers are its own (set on the thread that runs it)
  Collected euler;
  CHECK(core::run("r", starter_graph(), starter_station(), backend, euler.emit(), nullptr, {}, json{{"solver", "euler"}, {"duration", 0.1}}) == "done");
  CHECK(paperboat::toolkit::solve::make()->name() == "euler");
  paperboat::toolkit::solve::set_defaults({});

  // An infinite run (duration = 0) runs until cancelled
  std::stop_source stop;
  int steps_seen = 0;
  auto emit_fn = [&](const json& m) {
    if (m.value("type", "") == "signal") {
      if (++steps_seen >= 5) stop.request_stop();
    }
  };
  CHECK(core::run("inf", starter_graph(), starter_station(), backend, emit_fn, nullptr, stop.get_token(), json{{"duration", 0}}) == "cancelled");
  CHECK(steps_seen >= 5);
}

TEST_CASE("a feedback loop through a block without feedthrough runs; an algebraic loop fails, naming a block") {
  // x' = -k x as a loop: integrator → gain (k = -2) → back into the integrator. No station
  // object closes it: the integrator gives its state before it reads its input.
  const json loop = {
      {"nodes",
       {{{"id", "int"}, {"type", "integrator"}, {"params", {{"initial", 1.0}}}},
        {{"id", "k"}, {"type", "gain"}, {"params", {{"k", -2.0}}}},
        {{"id", "plot"}, {"type", "graph"}}}},
      {"edges",
       {{{"from", {"int", "y"}}, {"to", {"k", "u"}}},
        {{"from", {"k", "y"}}, {"to", {"int", "u"}}},
        {{"from", {"int", "y"}}, {"to", {"plot", "in"}}}}}};
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.dt = 0.01, .duration = 1, .emit_every = 1, .real_time = false}};
  Collected out;
  REQUIRE(core::run("r", loop, nullptr, backend, out.emit(), nullptr, {}) == "done");
  // Forward Euler of x' = -2x from 1: (1 - 0.02)^100 at t = 1; e^-2 ≈ 0.1353 in the limit.
  CHECK_THAT(out.last_signal("plot"), WithinAbs(std::pow(0.98, 100), 1e-12));

  const json algebraic = {{"nodes", {{{"id", "a"}, {"type", "gain"}}, {{"id", "b"}, {"type", "gain"}}}},
                          {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}}, {{"from", {"b", "y"}}, {"to", {"a", "u"}}}}}};
  Collected bad;
  CHECK(core::run("r", algebraic, nullptr, backend, bad.emit(), nullptr, {}) == "failed");
  CHECK(std::ranges::any_of(bad.messages, [](const json& m) {
    return m["type"] == "node.state" && m["state"] == "failed" && (m["node"] == "a" || m["node"] == "b");
  }));
  CHECK(std::ranges::any_of(bad.messages, [](const json& m) {
    return m["type"] == "log" && m["text"].get<std::string>().find("algebraic loop") != std::string::npos;
  }));
}

TEST_CASE("a delay gives its input from n clock steps ago, any value") {
  const json graph = {{"nodes",
                       {{{"id", "clock"}, {"type", "clock"}},
                        {{"id", "d"}, {"type", "delay"}, {"params", {{"steps", 3.0}, {"initial", -1.0}}}},
                        {{"id", "plot"}, {"type", "graph"}}}},
                      {"edges", {{{"from", {"clock", "t"}}, {"to", {"d", "u"}}}, {{"from", {"d", "y"}}, {"to", {"plot", "in"}}}}}};
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.dt = 0.1, .duration = 1, .emit_every = 1, .real_time = false}};
  Collected out;
  REQUIRE(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  std::vector<double> v;
  for (const auto& m : out.messages)
    if (m["type"] == "signal") v.push_back(m["v"]);
  REQUIRE(v.size() == 11);
  CHECK(v[0] == -1.0);
  CHECK(v[2] == -1.0);
  CHECK_THAT(v[3], WithinAbs(0.0, 1e-12));   // t = 0, three steps late
  CHECK_THAT(v[10], WithinAbs(0.7, 1e-12));  // t = 1.0 shows t = 0.7
}

TEST_CASE("session reports toolkits in hello and triggers restart handler on backend.restart") {
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = {.real_time = false}};
  std::vector<json> sent;
  std::mutex mutex;
  bool restarted = false;
  core::set_restart_handler([&] { restarted = true; });
  {
    core::Session session(backend, [&](const std::string& text) {
      std::lock_guard lock(mutex);
      sent.push_back(json::parse(text));
    });
    REQUIRE_FALSE(sent.empty());
    CHECK(sent.front()["type"] == "hello");
    if (sent.front().contains("toolkits")) {
      CHECK(sent.front()["toolkits"].is_array());
    }
    session.on_message(R"({"type": "backend.restart"})");
    for (int i = 0; i < 50 && !restarted; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  core::set_restart_handler(nullptr);
  CHECK(restarted);
  CHECK(std::ranges::any_of(sent, [](const json& m) {
    return m["type"] == "log" && m["text"] == "restarting backend…";
  }));
}

// --- Tunable variables (#75)

namespace {

const tk::Catalog& tune_catalog() {
  static const tk::Catalog catalog = tk::load_descriptor(R"(domain = "tune"
import = ["base", "math", "control", "dashboard"]
[[variables]]
name = "g"
value = 2
max = 10
tunable = true
[[variables]]
name = "h"
value = 3
[graph]
nodes = [
  { id = "one", type = "constant", params = { value = 1 } },
  { id = "live", type = "variable", params = { name = "g" } },
  { id = "held", type = "variable", params = { name = "h" } },
  { id = "gain", type = "gain", params = { k = { var = "g" } } },
  { id = "pid", type = "pid", params = { kp = { var = "g" } } },
  { id = "lag", type = "lag", params = { tau = { var = "g" } } },
  { id = "see-live", type = "display" }, { id = "see-held", type = "display" },
  { id = "see-gain", type = "display" }, { id = "see-pid", type = "display" },
]
edges = [
  { from = ["live", "y"], to = ["see-live", "in"] }, { from = ["held", "y"], to = ["see-held", "in"] },
  { from = ["one", "y"], to = ["gain", "u"] }, { from = ["gain", "y"], to = ["see-gain", "in"] },
  { from = ["one", "y"], to = ["pid", "e"] }, { from = ["pid", "u"], to = ["see-pid", "in"] },
  { from = ["one", "y"], to = ["lag", "u"] },
]
)", tk::builtins());
  return catalog;
}

json tuned_list(double g, double h) {
  return json::array({{{"name", "g"}, {"type", "number"}, {"value", g}, {"max", 10}, {"tunable", true}},
                      {{"name", "h"}, {"type", "number"}, {"value", h}}});
}

}  // namespace

TEST_CASE("a tunable variable reaches the Variable block and bound params during a run; the rest stay") {
  const auto& catalog = tune_catalog();
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 1, .emit_every = 1, .real_time = false}};
  const json graph = json::parse(catalog.graph);
  const json start = json::parse(catalog.variables);
  int calls = 0;
  const auto change = [&](json list) {
    return core::VariablesNow([&calls, list] { return ++calls == 30 ? std::optional<json>(list) : std::nullopt; });
  };
  auto warnings = [](const Collected& out) {
    std::string all;
    for (const auto& m : out.messages)
      if (m["type"] == "log" && m["level"] == "warn") all += m["text"].get<std::string>() + "\n";
    return all;
  };
  SECTION("without a change, a variable is constant for the run") {
    Collected out;
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}, nullptr, start) == "done");
    CHECK(out.last_signal("see-live") == 2.0);
    CHECK(out.last_signal("see-held") == 3.0);
    CHECK(out.last_signal("see-gain") == 2.0);
    CHECK(out.last_signal("see-pid") == 2.0);
  }
  SECTION("g is tunable: its Variable block, a stateless block (made again) and a PID (set_param) follow; h does not") {
    Collected out;
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}, nullptr, start, change(tuned_list(5, 9))) == "done");
    CHECK(out.last_signal("see-live") == 5.0);
    CHECK(out.last_signal("see-held") == 3.0);
    CHECK(out.last_signal("see-gain") == 5.0);
    CHECK(out.last_signal("see-pid") == 5.0);
    // a Lag has state and takes no live change: told once, and it keeps tau
    const auto told = warnings(out);
    CHECK_THAT(told, ContainsSubstring("lag.tau changes with the next run"));
    CHECK(std::ranges::count(told, '\n') == 1);
  }
  SECTION("a value its range refuses is ignored, with a word") {
    Collected out;
    CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}, nullptr, start, change(tuned_list(50, 3))) == "done");
    CHECK(out.last_signal("see-live") == 2.0);
    CHECK_THAT(warnings(out), ContainsSubstring("variable change ignored"));
  }
  SECTION("a Variable block naming nothing fails the run, on the block") {
    json bad = graph;
    bad["nodes"][1]["params"]["name"] = "nope";
    Collected out;
    CHECK(core::run("r", bad, nullptr, backend, out.emit(), nullptr, {}, nullptr, start) == "failed");
    CHECK(std::ranges::any_of(out.messages, [](const json& m) {
      return m["type"] == "log" && m["level"] == "error" && m.value("node", "") == "live" &&
             m["text"].get<std::string>().find("no variable called \"nope\"") != std::string::npos;
    }));
  }
}

TEST_CASE("variables.set during a run goes to the run, not to the scene") {
  const auto& catalog = tune_catalog();
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 0.6, .emit_every = 1, .real_time = true}};
  std::vector<json> sent;
  std::mutex mutex;
  core::Session session(backend, [&](const std::string& text) {
    std::lock_guard lock(mutex);
    sent.push_back(json::parse(text));
  });
  session.on_message(R"({"type": "graph.run", "graph": )" + catalog.graph + "}");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  session.on_message(R"({"type": "variables.set", "variables": )" + tuned_list(7, 9).dump() + "}");
  for (int i = 0; i < 500; ++i) {
    {
      std::lock_guard lock(mutex);
      if (sent.back()["type"] == "run.finished") break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::lock_guard lock(mutex);
  REQUIRE(sent.back()["type"] == "run.finished");
  double live = 0, held = 0;
  int scenes_after_start = 0;
  bool started = false;
  for (const auto& m : sent) {
    if (m["type"] == "run.started") started = true;
    if (started && m["type"] == "scene") ++scenes_after_start;
    if (m["type"] == "signal" && m["node"] == "see-live") live = m["v"];
    if (m["type"] == "signal" && m["node"] == "see-held") held = m["v"];
  }
  CHECK(live == 7.0);
  CHECK(held == 3.0);
  CHECK(scenes_after_start == 1);  // the run's own; the edit did not show the station again
}

TEST_CASE("a dashboard Switch runs from its descriptor params and is sampled as a signal") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"switches\"\nimport = [\"dashboard\"]\n", tk::builtins());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 0.5, .emit_every = 1, .real_time = false}};
  const auto graph = [](const std::string& mode) {
    return json{{"nodes", {{{"id", "sw"}, {"type", "switch"}, {"params", {{"mode", mode}, {"initial", false}}}},
                           {{"id", "d"}, {"type", "display"}}}},
                {"edges", {{{"from", {"sw", "y"}}, {"to", {"d", "in"}}}}}};
  };
  int calls = 0;
  SECTION("momentary: on while the control is held") {
    core::KeysNow keys = [&] { return ++calls > 10 ? tk::KeySet{"sw"} : tk::KeySet{}; };
    Collected out;
    CHECK(core::run("r", graph("momentary"), nullptr, backend, out.emit(), keys, {}) == "done");
    CHECK(out.last_signal("sw") == 1.0);  // the Switch itself is an instrument
    CHECK(out.last_signal("d") == 1.0);
  }
  SECTION("toggle: one tap seen for a single pass flips it once") {
    core::KeysNow keys = [&] { return ++calls == 10 ? tk::KeySet{"sw"} : tk::KeySet{}; };
    Collected out;
    CHECK(core::run("r", graph("toggle"), nullptr, backend, out.emit(), keys, {}) == "done");
    CHECK(out.last_signal("sw") == 1.0);
  }
}

TEST_CASE("commented out blocks are excluded from simulation and do not create cycles") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"test\"\nimport = [\"math\", \"dashboard\"]\n", tk::builtins());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 0.1, .emit_every = 1, .real_time = false}};

  // Loop a -> b -> a normally causes a cycle error.
  const json loop_normal = {
      {"nodes", {{{"id", "a"}, {"type", "gain"}, {"params", {{"k", 2.0}}}},
                 {{"id", "b"}, {"type", "gain"}, {"params", {{"k", 3.0}}}}}},
      {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}},
                 {{"from", {"b", "y"}}, {"to", {"a", "u"}}}}}};
  core::Station empty;
  CHECK_THROWS_WITH(core::prepare_graph(loop_normal, catalog, empty), ContainsSubstring("cycle"));

  // With block 'a' commented out (mode = "out"), no cycle error occurs and b receives default 0.0.
  const json loop_commented = {
      {"nodes", {{{"id", "a"}, {"type", "gain"}, {"params", {{"k", 2.0}}}, {"commented", true}, {"comment_mode", "out"}},
                 {{"id", "b"}, {"type", "gain"}, {"params", {{"k", 3.0}}}},
                 {{"id", "disp"}, {"type", "display"}}}},
      {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}},
                 {{"from", {"b", "y"}}, {"to", {"a", "u"}}},
                 {{"from", {"b", "y"}}, {"to", {"disp", "in"}}}}}};
  const auto g = core::prepare_graph(loop_commented, catalog, empty);
  CHECK(std::ranges::find(g.order, "a") == g.order.end());
  CHECK(std::ranges::find(g.order, "b") != g.order.end());

  Collected out;
  CHECK(core::run("r", loop_commented, nullptr, backend, out.emit(), nullptr, {}) == "done");
  CHECK(out.last_signal("disp") == 0.0);
}

TEST_CASE("comment through passes signal values directly through to output ports") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"test\"\nimport = [\"base\", \"math\", \"dashboard\"]\n", tk::builtins());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 0.1, .emit_every = 1, .real_time = false}};

  // Number 42.0 -> Gain (k = 5, commented through) -> Display
  const json graph = {
      {"nodes", {{{"id", "num"}, {"type", "constant"}, {"params", {{"value", 42.0}}}},
                 {{"id", "g"}, {"type", "gain"}, {"params", {{"k", 5.0}}}, {"commented", true}, {"comment_mode", "through"}},
                 {{"id", "disp"}, {"type", "display"}}}},
      {"edges", {{{"from", {"num", "y"}}, {"to", {"g", "u"}}},
                 {{"from", {"g", "y"}}, {"to", {"disp", "in"}}}}}};

  Collected out;
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  // Gain is bypassed, so input 42.0 passes straight through instead of 42 * 5 = 210.
  CHECK(out.last_signal("disp") == 42.0);
}

TEST_CASE("step block comment through immediately bypasses to next step output") {
  static const tk::Catalog catalog = tk::load_descriptor("domain = \"test\"\nimport = [\"step\"]\n", tk::builtins());
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.1, .duration = 2.0, .emit_every = 1, .real_time = false}};

  // start -> wait (10s, bypassed) -> stop
  const json graph = {
      {"nodes", {{{"id", "start"}, {"type", "start"}},
                 {{"id", "w"}, {"type", "wait"}, {"params", {{"seconds", 10.0}}}, {"commented", true}, {"comment_mode", "through"}},
                 {{"id", "stop"}, {"type", "stop"}}}},
      {"edges", {{{"from", {"start", "next"}}, {"to", {"w", "in"}}},
                 {{"from", {"w", "next"}}, {"to", {"stop", "in"}}}}}};

  Collected out;
  CHECK(core::run("r", graph, nullptr, backend, out.emit(), nullptr, {}) == "done");
  bool stopped_by_stop = false;
  for (const auto& m : out.messages) {
    if (m.value("type", "") == "log" && m.value("text", "").find("stopped by stop") != std::string::npos) {
      stopped_by_stop = true;
    }
  }
  CHECK(stopped_by_stop);
}

// --- Blocks write variables (#166)

namespace {

// Sets g to its `target` param's variable each step from the 10th: the way an autotuner would.
// It also tries what must be refused, and reads g back.
class Tuner final : public tk::Block {
 public:
  Tuner(const tk::Values&, const tk::Config& config) : target(config.at("target")) {}
  tk::Values step(const tk::Context& ctx, const tk::Values&) override {
    seen = ctx.variable("g") ? std::get<double>(*ctx.variable("g")) : -1;
    if (++n == 10) {
      CHECK(ctx.set_variable(target, 4.0));
      CHECK_FALSE(ctx.set_variable("h", 1.0));        // not tunable
      CHECK_FALSE(ctx.set_variable("twice", 1.0));    // a formula
      CHECK_FALSE(ctx.set_variable("nope", 1.0));     // unknown
      CHECK_FALSE(ctx.set_variable(target, 99.0));    // above its max
      CHECK_FALSE(ctx.set_variable(target, "text"));  // wrong type
      reads_before_commit = std::get<double>(*ctx.variable("g"));
    }
    return {{"y", seen}};
  }
  std::string target;
  int n = 0;
  double seen = 0;
  inline static double reads_before_commit = 0;
};

const tk::Catalog& writer_catalog() {
  static const tk::Registry registry = [] {
    tk::Registry r = tk::builtins();
    r.add(tk::define_block<Tuner>("tune", {.type = "tuner", .label = "Tuner", .outputs = {{"y"}},
                                           .params = {{.id = "target", .type = "variable", .default_text = "g"}}}));
    return r;
  }();
  static const tk::Catalog catalog = tk::load_descriptor(R"(domain = "tune"
import = ["base", "math", "control", "dashboard"]
[[variables]]
name = "g"
value = 2
max = 10
tunable = true
[[variables]]
name = "h"
value = 3
[[variables]]
name = "twice"
expr = "g * 2"
[graph]
nodes = [
  { id = "tuner", type = "tuner" },
  { id = "live", type = "variable", params = { name = "g" } },
  { id = "gain", type = "gain", params = { k = { var = "g" } } },
  { id = "one", type = "constant", params = { value = 1 } },
  { id = "see-seen", type = "display" }, { id = "see-live", type = "display" }, { id = "see-gain", type = "display" },
]
edges = [
  { from = ["tuner", "y"], to = ["see-seen", "in"] }, { from = ["live", "y"], to = ["see-live", "in"] },
  { from = ["one", "y"], to = ["gain", "u"] }, { from = ["gain", "y"], to = ["see-gain", "in"] },
]
)", registry);
  return catalog;
}

}  // namespace

TEST_CASE("a block sets a tunable variable: applied at the end of the pass, formulas and bound params follow, the UI is told") {
  const auto& catalog = writer_catalog();
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 1, .emit_every = 1, .real_time = false}};
  Collected out;
  CHECK(core::run("r", json::parse(catalog.graph), nullptr, backend, out.emit(), nullptr, {}, nullptr, json::parse(catalog.variables)) == "done");
  CHECK(Tuner::reads_before_commit == 2.0);  // not yet: the pass finishes with the old value
  CHECK(out.last_signal("see-seen") == 4.0);  // the next pass reads the new one
  CHECK(out.last_signal("see-live") == 4.0);
  CHECK(out.last_signal("see-gain") == 4.0);  // a param bound to it follows
  const auto patches = std::ranges::count_if(out.messages, [](const json& m) { return m["type"] == "variables.patch"; });
  REQUIRE(patches == 1);
  const auto patch = std::ranges::find_if(out.messages, [](const json& m) { return m["type"] == "variables.patch"; });
  CHECK(patch->at("values") == json({{"g", 4.0}, {"twice", 8.0}}));
  // each refusal warned once, on the block
  int warned = 0;
  for (const auto& m : out.messages)
    if (m["type"] == "log" && m["level"] == "warn" && m.value("node", "") == "tuner") ++warned;
  CHECK(warned == 5);
}

TEST_CASE("a variable a block set during a run is what the session starts the next run from") {
  const auto& catalog = writer_catalog();
  const core::Backend backend{.engine = "test", .catalog = &catalog, .run = {.dt = 0.01, .duration = 0.3, .emit_every = 1, .real_time = false}};
  std::vector<json> sent;
  std::mutex mutex;
  core::Session session(backend, [&](const std::string& text) {
    std::lock_guard lock(mutex);
    sent.push_back(json::parse(text));
  });
  const auto finished = [&](int count) {
    for (int i = 0; i < 500; ++i) {
      {
        std::lock_guard lock(mutex);
        if (std::ranges::count_if(sent, [](const json& m) { return m["type"] == "run.finished"; }) >= count) return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  };
  session.on_message(R"({"type": "graph.run", "graph": )" + catalog.graph + "}");
  finished(1);
  session.on_message(R"({"type": "graph.run", "graph": )" + catalog.graph + "}");
  finished(2);
  std::lock_guard lock(mutex);
  // the second run starts from the tuned g = 4: its first Variable sample reads 4
  bool second = false;
  double first_live = 0;
  int runs = 0;
  for (const auto& m : sent) {
    if (m["type"] == "run.started") second = ++runs == 2;
    if (second && m["type"] == "signal" && m["node"] == "see-live") {
      first_live = m["v"];
      break;
    }
  }
  CHECK(first_live == 4.0);
}
