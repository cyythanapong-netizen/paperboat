// The run cache (cache.hpp): a run that evaluates once reuses the outputs of blocks nothing
// changed for since an earlier run, and computes exactly the rest.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/cache.hpp"
#include "paperboat/core/executor.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;

namespace {

std::map<std::string, int>& calls() {  // node id → how many times its block was called
  static std::map<std::string, int> c;
  return c;
}

std::stop_source*& cancel_during_halt() {  // set: a Halt block cancels the run while it computes
  static std::stop_source* s = nullptr;
  return s;
}

// y = k·u + w; counts its calls and logs one line.
class Scale final : public tk::Block {
 public:
  explicit Scale(const tk::Values& p) : k_(tk::get_or(p, "k", 1)) {}
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    ++calls()[std::string(ctx.node)];
    ctx.log("scaled by " + std::to_string(static_cast<int>(k_)));
    return {{"y", k_ * tk::get_or(in, "u") + tk::get_or(in, "w")}};
  }

 private:
  double k_;
};

// y = u, unless the user cancels while it computes: then it stops short, with a wrong y.
class Halt final : public tk::Block {
 public:
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    ++calls()[std::string(ctx.node)];
    if (auto* stop = cancel_during_halt()) {
      stop->request_stop();
      return {{"y", -1.0}};
    }
    return {{"y", tk::get_or(in, "u")}};
  }
};

// y = u, and sets the variable `level` to u: a side effect, so never reused.
class Setter final : public tk::Block {
 public:
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    ++calls()[std::string(ctx.node)];
    ctx.set_variable("level", tk::get_or(in, "u"));
    return {{"y", tk::get_or(in, "u")}};
  }
};

tk::Template scale_block(const std::string& type, std::optional<bool> cacheable) {
  auto t = tk::define_block<Scale>("rig", {.type = type,
                                           .label = type,
                                           .inputs = {{"u"}, {"w"}},
                                           .outputs = {{"y"}},
                                           .params = {{.id = "k", .default_value = 1}}});
  t.def.cacheable = cacheable;
  return t;
}

const tk::Catalog& catalog() {
  static const tk::Registry registry = [] {
    tk::Registry r = rig::registry();
    r.add(scale_block("scale", true));   // opts in
    r.add(scale_block("fresh", false));  // opts out
    r.add(scale_block("plain", {}));     // says nothing, and isn't stateless: called every time
    auto halt = tk::define_block<Halt>("rig", {.type = "halt", .label = "Halt", .inputs = {{"u"}}, .outputs = {{"y"}}});
    halt.def.cacheable = true;
    r.add(std::move(halt));
    auto setter = tk::define_block<Setter>("rig", {.type = "setter", .label = "Setter", .inputs = {{"u"}}, .outputs = {{"y"}}});
    setter.def.cacheable = true;
    r.add(std::move(setter));
    return r;
  }();
  static const tk::Catalog c = tk::load_descriptor(R"(domain = "rig"
import = ["base", "math", "string", "dashboard"]
)", registry);
  return c;
}

const core::Backend& once_backend() {
  static const core::Backend b{.engine = "test", .catalog = &catalog(), .run = {.mode = core::RunSettings::Mode::Once, .real_time = false}};
  return b;
}

json node(const std::string& id, const std::string& type, json params = json::object()) {
  return {{"id", id}, {"type", type}, {"params", std::move(params)}};
}
json wire(const std::string& from, const std::string& out, const std::string& to, const std::string& in) {
  return {{"from", {from, out}}, {"to", {to, in}}};
}

using Ids = std::set<std::string>;

struct Result {
  std::string status;
  std::map<std::string, std::string> state;  // node → its last node.state
  Ids ran;                                   // nodes reported running: called
  std::map<std::string, double> shown;       // monitor → the last value it showed
  std::vector<std::string> logs;
  std::vector<json> messages;

  // A reused block is reported cached with no running before it; a called one, running then done.
  Ids last(const std::string& st) const {
    Ids out;
    for (const auto& [n, s] : state)
      if (s == st) out.insert(n);
    return out;
  }
  Ids cached() const {
    Ids out = last("cached");
    for (const auto& n : out) REQUIRE(!ran.contains(n));
    return out;
  }
  Ids computed() const {
    Ids out = last("done");
    for (const auto& n : out) REQUIRE(ran.contains(n));
    return out;
  }
};

Result run(const json& graph, core::RunCache* cache, const json& variables = nullptr, const json& station = nullptr,
           const std::stop_token& stop = {}, const core::Backend& backend = once_backend()) {
  Result r;
  const core::Emit emit = [&](const json& m) {
    r.messages.push_back(m);
    if (m["type"] == "node.state") r.state[m["node"]] = m["state"];
    if (m["type"] == "node.state" && m["state"] == "running") r.ran.insert(m["node"]);
    if (m["type"] == "signal" && m["v"].is_number()) r.shown[m["node"]] = m["v"];
    if (m["type"] == "log") r.logs.push_back(m["text"]);
  };
  r.status = core::run("r", graph, station, backend, emit, nullptr, stop, nullptr, variables, nullptr, nullptr, cache);
  return r;
}

// a (2) → f (×3) → g (×1) → show-g;  b (5) → h (×10) → show-h
json chain() {
  return {{"nodes", {node("a", "constant", {{"value", 2}}), node("b", "constant", {{"value", 5}}), node("f", "scale", {{"k", 3}}),
                     node("g", "scale", {{"k", 1}}), node("h", "scale", {{"k", 10}}), node("show-g", "display"),
                     node("show-h", "display")}},
          {"edges", {wire("a", "y", "f", "u"), wire("f", "y", "g", "u"), wire("b", "y", "h", "u"), wire("g", "y", "show-g", "in"),
                     wire("h", "y", "show-h", "in")}}};
}

const Ids monitors = {"show-g", "show-h"};  // instruments: always shown afresh

Ids with_monitors(Ids ids) {
  ids.insert(monitors.begin(), monitors.end());
  return ids;
}

}  // namespace

TEST_CASE("cache: a second identical run reuses every block's outputs") {
  calls().clear();
  core::RunCache cache;
  const auto first = run(chain(), &cache);
  REQUIRE(first.status == "done");
  CHECK(first.cached().empty());
  CHECK(first.computed() == with_monitors({"a", "b", "f", "g", "h"}));
  CHECK(first.shown.at("show-g") == 6.0);
  CHECK(first.shown.at("show-h") == 50.0);
  CHECK(cache.size() == 5);  // the blocks; never the monitors

  const auto second = run(chain(), &cache);
  REQUIRE(second.status == "done");
  CHECK(second.cached() == Ids{"a", "b", "f", "g", "h"});
  CHECK(second.computed() == monitors);
  CHECK(calls() == std::map<std::string, int>{{"f", 1}, {"g", 1}, {"h", 1}});  // not called again
  CHECK(second.shown == first.shown);
  CHECK(second.logs.back() == "evaluated 7 blocks (5 reused from an earlier run)");
  // What a reused block wrote is shown again, tagged with it.
  CHECK(std::ranges::any_of(second.messages, [](const json& m) {
    return m["type"] == "log" && m.value("node", "") == "f" && m["text"] == "scaled by 3";
  }));
  // A reused block is reported queued, then cached, never running; a called one queued, running, done.
  const auto states_of = [](const Result& r, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& m : r.messages)
      if (m["type"] == "node.state" && m["node"] == id) out.push_back(m["state"]);
    return out;
  };
  CHECK(states_of(first, "f") == std::vector<std::string>{"queued", "running", "done"});
  CHECK(states_of(second, "f") == std::vector<std::string>{"queued", "cached"});
  CHECK(states_of(second, "show-g") == std::vector<std::string>{"queued", "running", "done"});
  // Every block is queued before the first one runs.
  const auto first_running = std::ranges::find_if(second.messages, [](const json& m) {
    return m["type"] == "node.state" && m["state"] != "queued";
  });
  CHECK(std::count_if(second.messages.begin(), first_running, [](const json& m) {
          return m["type"] == "node.state" && m["state"] == "queued";
        }) == 7);
}

TEST_CASE("cache: a changed param, wire or variable recomputes exactly the blocks after it") {
  calls().clear();
  core::RunCache cache;
  REQUIRE(run(chain(), &cache).status == "done");

  SECTION("a param") {
    auto graph = chain();
    graph["nodes"][2]["params"]["k"] = 4;  // f
    const auto r = run(graph, &cache);
    CHECK(r.computed() == with_monitors({"f", "g"}));
    CHECK(r.cached() == Ids{"a", "b", "h"});
    CHECK(r.shown.at("show-g") == 8.0);
    CHECK(r.shown.at("show-h") == 50.0);
    CHECK(calls() == std::map<std::string, int>{{"f", 2}, {"g", 2}, {"h", 1}});
    // Back to the first value: computed again (the cache keeps a block's last result only).
    CHECK(run(chain(), &cache).computed() == with_monitors({"f", "g"}));
  }
  SECTION("a wire") {
    auto graph = chain();
    graph["edges"][1] = wire("b", "y", "g", "u");  // g now reads b, not f
    const auto r = run(graph, &cache);
    CHECK(r.computed() == with_monitors({"g"}));
    CHECK(r.cached() == Ids{"a", "b", "f", "h"});
    CHECK(r.shown.at("show-g") == 5.0);
  }
  SECTION("a wire added to an input that read 0") {
    auto graph = chain();
    graph["edges"].push_back(wire("b", "y", "g", "w"));
    const auto r = run(graph, &cache);
    CHECK(r.computed() == with_monitors({"g"}));
    CHECK(r.shown.at("show-g") == 11.0);
  }
  SECTION("an upstream change that gives the same values stops there") {
    auto graph = chain();
    graph["nodes"][0]["params"]["value"] = 2.0;  // the same number, written otherwise
    graph["nodes"][2]["params"]["k"] = 3;
    CHECK(run(graph, &cache).cached() == Ids{"a", "b", "f", "g", "h"});
    graph["nodes"][0]["params"]["value"] = 4;  // a: 4 → f: 12 → g: 12
    graph["nodes"][2]["params"]["k"] = 1.5;    // f: 1.5 · 4 = 6, as before
    const auto r = run(graph, &cache);
    CHECK(r.computed() == with_monitors({"a", "f"}));
    CHECK(r.cached() == Ids{"b", "g", "h"});  // g's input is the same 6: reused
    CHECK(r.shown.at("show-g") == 6.0);
  }
  SECTION("a node removed is forgotten after a run that is done") {
    auto graph = chain();
    graph["nodes"].erase(4);  // h
    graph["nodes"].erase(5);  // show-h (now at 5)
    graph["edges"].erase(4);
    graph["edges"].erase(2);
    REQUIRE(run(graph, &cache).status == "done");
    CHECK(!cache.contains("h"));
    CHECK(cache.size() == 4);
  }
}

TEST_CASE("cache: a variable a block reads, or a param is bound to, invalidates it alone") {
  calls().clear();
  core::RunCache cache;
  // f's k is the variable gain; lv reads the variable level; m = lv × 2.
  auto graph = chain();
  graph["nodes"][2]["params"]["k"] = {{"var", "gain"}};
  graph["nodes"].push_back(node("lv", "variable", {{"name", "level"}}));
  graph["nodes"].push_back(node("m", "scale", {{"k", 2}}));
  graph["nodes"].push_back(node("show-m", "display"));
  graph["edges"].push_back(wire("lv", "y", "m", "u"));
  graph["edges"].push_back(wire("m", "y", "show-m", "in"));
  const auto variables = [](double gain, double level, bool other = false) {
    json list = json::array({{{"name", "gain"}, {"type", "number"}, {"value", gain}}, {{"name", "level"}, {"type", "number"}, {"value", level}}});
    if (other) list.push_back({{"name", "other"}, {"type", "number"}, {"value", 1}});
    return list;
  };
  const auto first = run(graph, &cache, variables(3, 7));
  REQUIRE(first.status == "done");
  CHECK(first.shown.at("show-g") == 6.0);
  CHECK(first.shown.at("show-m") == 14.0);

  const auto level = run(graph, &cache, variables(3, 8));  // read by lv only
  CHECK(level.cached() == Ids{"a", "b", "f", "g", "h"});
  CHECK(level.computed() == with_monitors({"lv", "m", "show-m"}));
  CHECK(level.shown.at("show-m") == 16.0);

  const auto gain = run(graph, &cache, variables(5, 8));  // f's param
  CHECK(gain.cached() == Ids{"a", "b", "h", "lv", "m"});
  CHECK(gain.computed() == with_monitors({"f", "g", "show-m"}));
  CHECK(gain.shown.at("show-g") == 10.0);

  const auto other = run(graph, &cache, variables(5, 8, true));  // a variable no block uses
  CHECK(other.cached() == Ids{"a", "b", "f", "g", "h", "lv", "m"});

  graph["nodes"][7]["params"]["name"] = "nope";  // lv: a variable that isn't there fails, and is not kept
  const auto missing = run(graph, &cache, variables(5, 8));
  CHECK(missing.status == "failed");
  CHECK(missing.state.at("lv") == "failed");
}

TEST_CASE("cache: opted-out blocks are always called; what follows them is reused if their values hold") {
  calls().clear();
  core::RunCache cache;
  // r opts out, p says nothing (and isn't stateless), gn is a built-in Gain (stateless: reused).
  const json graph = {{"nodes", {node("a", "constant", {{"value", 2}}), node("r", "fresh", {{"k", 2}}), node("after", "scale", {{"k", 1}}),
                                 node("p", "plain", {{"k", 3}}), node("gn", "gain", {{"k", 4}}), node("show", "display")}},
                      {"edges", {wire("a", "y", "r", "u"), wire("r", "y", "after", "u"), wire("a", "y", "p", "u"), wire("a", "y", "gn", "u"),
                                 wire("after", "y", "show", "in")}}};
  for (int i = 1; i <= 3; ++i) {
    const auto r = run(graph, &cache);
    REQUIRE(r.status == "done");
    CHECK(r.state.at("r") == "done");
    CHECK(r.state.at("p") == "done");
    CHECK(calls().at("r") == i);
    CHECK(calls().at("p") == i);
    if (i > 1) CHECK(r.cached() == Ids{"a", "after", "gn"});
    CHECK(r.shown.at("show") == 4.0);
  }
  CHECK(calls().at("after") == 1);
  CHECK(!cache.contains("r"));
  CHECK(!cache.contains("p"));
}

TEST_CASE("cache: built-in blocks that read keys, the clock or draw at random opt out") {
  const auto& c = catalog();
  for (const char* type : {"random_number", "random_boolean", "key", "key_hold", "now"}) {
    INFO(type);
    REQUIRE(c.find(type) != nullptr);
    CHECK(c.find(type)->def.cacheable == std::optional<bool>(false));
  }
  for (const char* type : {"variable", "text", "format", "concat", "to_number", "and", "if"}) {
    INFO(type);
    REQUIRE(c.find(type) != nullptr);
    CHECK(c.find(type)->def.cacheable == std::optional<bool>(true));
  }
}

TEST_CASE("cache: a block that sets a variable is never reused") {
  calls().clear();
  core::RunCache cache;
  const json graph = {{"nodes", {node("a", "constant", {{"value", 2}}), node("s", "setter")}}, {"edges", {wire("a", "y", "s", "u")}}};
  const json variables = json::array({{{"name", "level"}, {"type", "number"}, {"value", 2}, {"tunable", true}}});
  REQUIRE(run(graph, &cache, variables).status == "done");
  const auto second = run(graph, &cache, variables);
  CHECK(second.computed() == Ids{"s"});
  CHECK(second.cached() == Ids{"a"});
  CHECK(calls().at("s") == 2);
  CHECK(!cache.contains("s"));
}

TEST_CASE("cache: a cancelled run does not poison the cache") {
  calls().clear();
  core::RunCache cache;
  // a (2) → c1 (×3) → x (halt) → c2 (×1) → show
  const json graph = {{"nodes", {node("a", "constant", {{"value", 2}}), node("c1", "scale", {{"k", 3}}), node("x", "halt"),
                                 node("c2", "scale", {{"k", 1}}), node("show", "display")}},
                      {"edges", {wire("a", "y", "c1", "u"), wire("c1", "y", "x", "u"), wire("x", "y", "c2", "u"), wire("c2", "y", "show", "in")}}};

  SECTION("a block cut short by the cancel is not kept; the blocks done before it are") {
    std::stop_source stop;
    cancel_during_halt() = &stop;
    const auto cancelled = run(graph, &cache, nullptr, nullptr, stop.get_token());
    cancel_during_halt() = nullptr;
    REQUIRE(cancelled.status == "cancelled");
    CHECK(calls().at("x") == 1);
    CHECK(!calls().contains("c2"));  // stopped between x and c2
    CHECK(cache.contains("c1"));
    CHECK(!cache.contains("x"));  // its -1 was not its result
    CHECK(!cache.contains("c2"));

    const auto next = run(graph, &cache);
    REQUIRE(next.status == "done");
    CHECK(next.cached() == Ids{"a", "c1"});
    CHECK(next.computed() == Ids{"x", "c2", "show"});
    CHECK(next.shown.at("show") == 6.0);  // not -1

    const auto again = run(graph, &cache);
    CHECK(again.cached() == Ids{"a", "c1", "x", "c2"});
    CHECK(again.shown.at("show") == 6.0);
  }
  SECTION("a cancelled run keeps what earlier runs left, even of nodes it doesn't have") {
    REQUIRE(run(graph, &cache).status == "done");
    REQUIRE(cache.size() == 4);
    std::stop_source stop;
    stop.request_stop();
    const json smaller = {{"nodes", {node("a", "constant", {{"value", 2}})}}, {"edges", json::array()}};
    CHECK(run(smaller, &cache, nullptr, nullptr, stop.get_token()).status == "cancelled");
    CHECK(cache.size() == 4);
    CHECK(run(graph, &cache).cached() == Ids{"a", "c1", "x", "c2"});
  }
}

TEST_CASE("cache: a station object changed invalidates only the blocks it feeds") {
  calls().clear();
  core::RunCache cache;
  const auto station = [](double x2) {
    return json{{"objects", {{{"id", "s1"}, {"type", "slider"}, {"props", {{"x0", 1}}}}, {{"id", "s2"}, {"type", "slider"}, {"props", {{"x0", x2}}}}}}};
  };
  const json graph = {{"nodes", {node("s1", "slider"), node("s2", "slider"), node("f1", "scale", {{"k", 2}}), node("f2", "scale", {{"k", 2}}),
                                 node("show-1", "display"), node("show-2", "display")}},
                      {"edges", {wire("s1", "x", "f1", "u"), wire("s2", "x", "f2", "u"), wire("f1", "y", "show-1", "in"), wire("f2", "y", "show-2", "in")}}};
  REQUIRE(run(graph, &cache, nullptr, station(2)).status == "done");
  const auto r = run(graph, &cache, nullptr, station(3));
  REQUIRE(r.status == "done");
  CHECK(r.cached() == Ids{"f1"});
  CHECK(r.state.at("f2") == "done");
  CHECK(r.shown.at("show-1") == 2.0);
  CHECK(r.shown.at("show-2") == 6.0);
  CHECK(calls() == std::map<std::string, int>{{"f1", 1}, {"f2", 2}});
}

TEST_CASE("cache: runs on a clock, and runs given no cache, call every block") {
  calls().clear();
  SECTION("on a clock") {
    core::RunCache cache;
    const core::Backend clock{.engine = "test", .catalog = &catalog(), .run = {.dt = 0.01, .duration = 0.02, .real_time = false}};
    for (int i = 0; i < 2; ++i) {
      const auto r = run(chain(), &cache, nullptr, nullptr, {}, clock);
      REQUIRE(r.status == "done");
      CHECK(r.cached().empty());
    }
    CHECK(calls().at("f") == 6);  // 3 steps a run
    CHECK(cache.size() == 0);
  }
  SECTION("no cache") {
    for (int i = 0; i < 2; ++i) CHECK(run(chain(), nullptr).cached().empty());
    CHECK(calls().at("f") == 2);
  }
}

TEST_CASE("cache: a session's run-once reuses its previous run (node.state cached, never running)") {
  calls().clear();
  std::vector<json> sent;
  std::mutex mutex;
  core::Session session(once_backend(), [&](const std::string& text) {
    std::lock_guard lock(mutex);
    sent.push_back(json::parse(text));
  });
  const auto finished = [&] {
    std::lock_guard lock(mutex);
    return std::ranges::count_if(sent, [](const json& m) { return m["type"] == "run.finished"; });
  };
  const auto run_and_wait = [&](long n) {
    session.on_message(json{{"type", "graph.run"}, {"graph", chain()}}.dump());
    for (int i = 0; i < 500 && finished() < n; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(finished() == n);
  };
  run_and_wait(1);
  run_and_wait(2);
  std::lock_guard lock(mutex);
  std::vector<std::string> second;
  bool in_second = false;
  for (const auto& m : sent) {
    if (m["type"] == "run.started") in_second = m["run"] == "run-2";
    if (in_second && m["type"] == "node.state" && m["node"] == "f") second.push_back(m["state"]);
  }
  CHECK(second == std::vector<std::string>{"queued", "cached"});
  CHECK(calls().at("f") == 1);
}

TEST_CASE("cache: values compare exactly") {
  using tk::Value;
  CHECK(core::same_value(Value(1.0), Value(1.0)));
  CHECK(!core::same_value(Value(0.0), Value(-0.0)));
  CHECK(core::same_value(Value(std::nan("")), Value(std::nan(""))));
  CHECK(!core::same_value(Value(1.0), Value(tk::Vector{1.0})));
  CHECK(core::same_value(Value(tk::Table{{1, 2}, {3}}), Value(tk::Table{{1, 2}, {3}})));
  CHECK(!core::same_value(Value("a"), Value("b")));
  const auto image = tk::make_handle("image", std::vector<int>{1, 2, 3});
  CHECK(core::same_value(Value(image), Value(image)));                                          // the same object
  CHECK(!core::same_value(Value(image), Value(tk::make_handle("image", std::vector<int>{1, 2, 3}))));  // an equal copy: not known to be
  CHECK(core::same_values({{"a", 1.0}, {"b", "x"}}, {{"a", 1.0}, {"b", "x"}}));
  CHECK(!core::same_values({{"a", 1.0}}, {{"a", 1.0}, {"b", 1.0}}));
}

TEST_CASE("cache: an elementwise block fed a vector (lanes) is reused like itself") {
  core::RunCache cache;
  auto graph = json{{"nodes", {node("x", "constant", {{"value", 1}}), node("v", "constant", {{"value", 2}}), node("m", "mux", {{"names", "x v"}}),
                               node("k", "gain", {{"k", 2}}), node("show", "display")}},
                    {"edges", {wire("x", "y", "m", "x"), wire("v", "y", "m", "v"), wire("m", "y", "k", "u"), wire("k", "y", "show", "in")}}};
  REQUIRE(run(graph, &cache).status == "done");
  CHECK(run(graph, &cache).cached() == Ids{"x", "v", "m", "k"});
  graph["nodes"][1]["params"]["value"] = 3;
  const auto r = run(graph, &cache);
  CHECK(r.computed() == Ids{"v", "m", "k", "show"});
  const auto shown = std::ranges::find_if(r.messages.rbegin(), r.messages.rend(), [](const json& m) { return m["type"] == "signal" && m["node"] == "show"; });
  REQUIRE(shown != r.messages.rend());
  CHECK((*shown)["v"] == json::array({2.0, 6.0}));
}
