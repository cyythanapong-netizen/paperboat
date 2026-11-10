// Flow types (#9): which flow types a block works in (package defaults, a block's own, a
// descriptor's override), which ones a domain offers (hello), and the engine refusing a block
// on a sheet of another type.

#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/graph.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using json = nlohmann::json;
using Catch::Matchers::ContainsSubstring;
using Flows = std::vector<std::string>;

namespace {

const tk::BlockDef& builtin(const char* name) {
  const tk::Template* t = tk::builtins().find(name);
  REQUIRE(t != nullptr);
  return t->def;
}

// Every block of the catalog message by type.
json blocks_of(const tk::Catalog& catalog) {
  const json message = json::parse(tk::catalog_message(catalog));
  json out = json::object();
  for (const auto& b : message.at("blocks")) out[b.at("type").get<std::string>()] = b;
  return out;
}

struct Pulse final : tk::Step {
  std::string tick(const tk::Context&, const tk::Values&) override { return "next"; }
};
struct Twice final : tk::Block {
  tk::Values step(const tk::Context&, const tk::Values& in) override { return {{"y", 2 * tk::get_or(in, "u")}}; }
};

const tk::Catalog& mixed() {
  static const tk::Catalog catalog = tk::load_descriptor(R"(domain = "mixed"
import = ["step", "base", "signal", "math", "dashboard"]
[[blocks]]
use = "math/gain"
type = "signal-only"
label = "Signal only"
flows = ["signal"]
)");
  return catalog;
}

// Start → Wait, and a Signal-only block beside them.
json step_graph() {
  return {{"nodes",
           {{{"id", "start"}, {"type", "start"}},
            {{"id", "wait"}, {"type", "wait"}},
            {{"id", "one"}, {"type", "constant"}},
            {{"id", "pid"}, {"type", "signal-only"}}}},
          {"edges", {{{"from", {"start", "next"}}, {"to", {"wait", "in"}}}, {{"from", {"one", "y"}}, {"to", {"pid", "u"}}}}}};
}

std::string refusal(const json& graph, const tk::Catalog& catalog, std::string* node = nullptr) {
  core::Station none;
  try {
    core::prepare_graph(graph, catalog, none);
  } catch (const core::GraphError& err) {
    if (node) *node = err.node;
    return err.what();
  }
  return "";
}

}  // namespace

TEST_CASE("packages say which flow types their blocks work in (#9)") {
  // base, signal, math, dashboard: every flow type (no list).
  for (const char* name : {"base/constant", "base/key", "base/if", "signal/sine", "signal/clock", "math/gain", "math/sum", "dashboard/graph"}) {
    INFO(name);
    CHECK(builtin(name).flows.empty());
    for (const auto& flow : tk::flow_types()) CHECK(tk::works_in(builtin(name), flow));
  }
  for (const char* name : {"step/start", "step/when", "step/wait", "step/repeat", "step/stop", "step/say"}) {
    INFO(name);
    CHECK(builtin(name).flows == Flows{"step"});
    CHECK_FALSE(tk::works_in(builtin(name), "signal"));
  }
  CHECK(tk::flow_types() == Flows{"signal", "step", "state", "model"});
  CHECK(tk::flow_label("step") == "Step flow");
  CHECK(tk::flows_label({"signal", "step"}) == "Signal flow or Step flow");
}

TEST_CASE("a block in code: a step works in a Step flow unless it says; a package default fills the rest") {
  tk::Registry r;
  r.add(tk::define_block<Pulse>("dom", {.type = "pulse", .label = "Pulse", .inputs = {{"in", "step"}}, .outputs = {{"next", "step"}}}));
  r.add(tk::define_block<Twice>("dom", {.type = "twice", .label = "Twice", .inputs = {{"u"}}, .outputs = {{"y"}}}));
  r.add(tk::define_block<Twice>("dom", {.type = "model-only", .label = "Model only", .inputs = {{"u"}}, .outputs = {{"y"}}, .flows = {"model"}}));
  CHECK(r.find("dom/pulse")->def.flows == Flows{"step"});
  CHECK(r.find("dom/twice")->def.flows.empty());
  r.default_flows("dom", {"signal", "state"});
  CHECK(r.find("dom/twice")->def.flows == Flows{"signal", "state"});
  CHECK(r.find("dom/pulse")->def.flows == Flows{"step"});        // its own stays
  CHECK(r.find("dom/model-only")->def.flows == Flows{"model"});  // and so does this one's
  CHECK_THROWS_WITH(r.default_flows("dom", {"liquid"}), ContainsSubstring("no flow type \"liquid\" (signal, step, state, model)"));
  CHECK_THROWS_WITH(r.default_flows("dom", {"step", "step"}), ContainsSubstring("\"step\" is listed twice"));
}

TEST_CASE("a descriptor entry overrides a block's flow types, and the catalog carries them") {
  const auto catalog = tk::load_descriptor(R"(domain = "d"
import = ["base", "math", "step"]
[[blocks]]
use = "math/gain"
type = "gain-anywhere"
flows = ["signal", "step"]
[[blocks]]
use = "base/constant"
type = "model-constant"
flows = ["model"]
)");
  CHECK(catalog.find("gain-anywhere")->def.flows == Flows{"signal", "step"});
  CHECK(catalog.find("model-constant")->def.flows == Flows{"model"});
  const json blocks = blocks_of(catalog);
  CHECK(blocks.at("gain-anywhere").at("flows") == json::array({"signal", "step"}));
  CHECK_FALSE(blocks.at("gain").contains("flows"));
  CHECK(blocks.at("start").at("flows") == json::array({"step"}));
  CHECK_FALSE(blocks.at("constant").contains("flows"));

  const auto bad = [](const std::string& flows) {
    return "domain = \"d\"\n[[blocks]]\nuse = \"math/gain\"\nflows = " + flows + "\n";
  };
  CHECK_THROWS_WITH(tk::load_descriptor(bad("[\"liquid\"]")), ContainsSubstring("blocks[0] (math/gain): no flow type \"liquid\""));
  CHECK_THROWS_WITH(tk::load_descriptor(bad("[]")), ContainsSubstring("flows is empty; leave it out for every flow type"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad("\"signal\"")), ContainsSubstring("flows is a list of flow types"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad("[\"step\", \"step\"]")), ContainsSubstring("listed twice"));
}

TEST_CASE("the flow types a domain offers: the descriptor's, else Signal flow and those its blocks name") {
  CHECK(tk::load_descriptor("domain = \"a\"\nimport = [\"base\", \"math\", \"dashboard\"]\n").flows == Flows{"signal"});
  CHECK(tk::load_descriptor("domain = \"a\"\nimport = [\"base\"]\n").flows == Flows{"signal"});
  CHECK(tk::load_descriptor("domain = \"a\"\nimport = [\"step\", \"base\"]\n").flows == Flows{"signal", "step"});
  CHECK(tk::load_descriptor("domain = \"a\"\nimport = [\"base\"]\n[[blocks]]\nuse = \"base/constant\"\ntype = \"m\"\nflows = [\"model\"]\n").flows ==
        Flows{"signal", "model"});
  CHECK(tk::load_descriptor("domain = \"a\"\nflows = [\"step\"]\nimport = [\"step\", \"base\"]\n").flows == Flows{"step"});
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"a\"\nflows = [\"liquid\"]\nimport = [\"base\"]\n"),
                    ContainsSubstring("descriptor: no flow type \"liquid\""));
}

TEST_CASE("a starter graph's flow type is checked at load") {
  const std::string head = "domain = \"a\"\nimport = [\"step\", \"base\"]\n[graph]\n";
  const auto ok = tk::load_descriptor(head + "flow = \"step\"\nnodes = [{ id = \"s\", type = \"start\" }, { id = \"c\", type = \"constant\" }]\n");
  CHECK(json::parse(ok.graph)["flow"] == "step");
  CHECK_FALSE(json::parse(tk::load_descriptor(head + "nodes = []\n").graph).contains("flow"));  // not chosen
  CHECK_THROWS_WITH(tk::load_descriptor(head + "flow = \"signal\"\nnodes = [{ id = \"s\", type = \"start\" }]\n"),
                    ContainsSubstring("graph: block s (Start) works only in a Step flow, not on this Signal flow sheet"));
  CHECK_THROWS_WITH(tk::load_descriptor(head + "flow = \"model\"\nnodes = []\n"),
                    ContainsSubstring("this sheet is a Model flow, which this backend doesn't offer (it offers a Signal flow or Step flow)"));
  CHECK_THROWS_WITH(tk::load_descriptor(head + "flow = \"liquid\"\nnodes = []\n"), ContainsSubstring("graph.flow must be a flow type"));
}

TEST_CASE("the engine refuses a block on a sheet of a flow type it doesn't work in") {
  const auto& catalog = mixed();
  core::Station none;
  // Not chosen (a graph from before sheets had a type): nothing is refused.
  CHECK_NOTHROW(core::prepare_graph(step_graph(), catalog, none));

  // A Step flow sheet: the Signal-only block is refused, naming it.
  json g = step_graph();
  g["flow"] = "step";
  std::string node;
  CHECK(refusal(g, catalog, &node) ==
        "block pid (Signal only) works only in a Signal flow, not on this Step flow sheet: remove it, or make the sheet a Signal flow");
  CHECK(node == "pid");
  // …commented out, it isn't run, so it may stay.
  g["nodes"][3]["commented"] = true;
  CHECK(refusal(g, catalog).empty());
  // …and in a story (whose blocks run as a Signal flow) it works.
  g = step_graph();
  g["flow"] = "step";
  g["edges"][0] = {{"from", {"start", "next"}}, {"to", {"tune", "in"}}};
  g["edges"].push_back({{"from", {"tune", "next"}}, {"to", {"wait", "in"}}});
  g["groups"] = {{{"id", "tune"}, {"kind", "story"}, {"nodes", {"one", "pid"}}}};
  CHECK(refusal(g, catalog).empty());
  // A step block in a story is in a Signal flow there.
  g["groups"][0]["nodes"].push_back("wait");
  CHECK_THAT(refusal(g, catalog, &node), ContainsSubstring("block wait (Wait) works only in a Step flow, not in story tune: a story runs its blocks as a Signal flow"));
  CHECK(node == "wait");
  // …and so is one in a subsystem inside a story.
  g["groups"][0]["nodes"].erase(2);
  g["groups"].push_back({{"id", "inner"}, {"kind", "subsystem"}, {"parent", "tune"}, {"nodes", {"wait"}}});
  CHECK_THAT(refusal(g, catalog), ContainsSubstring("not in story tune"));

  // A Signal flow sheet: step blocks are refused.
  g = step_graph();
  g["flow"] = "signal";
  CHECK(refusal(g, catalog, &node) ==
        "block start (Start) works only in a Step flow, not on this Signal flow sheet: remove it, or make the sheet a Step flow");
  CHECK(node == "start");
  g["nodes"] = {{{"id", "one"}, {"type", "constant"}}, {{"id", "pid"}, {"type", "signal-only"}}};
  g["edges"] = {{{"from", {"one", "y"}}, {"to", {"pid", "u"}}}};
  CHECK(refusal(g, catalog).empty());

  // A flow type the backend doesn't offer.
  g["flow"] = "model";
  CHECK_THAT(refusal(g, catalog, &node), ContainsSubstring("this sheet is a Model flow, which this backend doesn't offer"));
  CHECK(node.empty());
}

TEST_CASE("hello says which flow types the backend offers") {
  const core::Backend backend{.engine = "test", .catalog = &mixed(), .run = {.real_time = false}};
  std::vector<json> sent;
  std::mutex mutex;
  {
    core::Session session(backend, [&](const std::string& text) {
      std::lock_guard lock(mutex);
      sent.push_back(json::parse(text));
    });
  }
  REQUIRE_FALSE(sent.empty());
  CHECK(sent.front()["type"] == "hello");
  CHECK(sent.front().at("flows") == json::array({"signal", "step"}));
}
