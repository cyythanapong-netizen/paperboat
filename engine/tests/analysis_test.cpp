// Toolkit analyses (pb#370): a package registers one, the catalog lists it, and a session
// answers analysis.request with the part of the graph made as a run makes it.

#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/toolkit/analysis.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/registry.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using json = nlohmann::json;
using Catch::Matchers::ContainsSubstring;

namespace {

// "probe/shape": what it was given, so the test can see it. Fails on an option, to show errors travel.
const tk::Registry& registry() {
  static const tk::Registry r = [] {
    tk::Registry out = tk::builtins();
    out.add(tk::AnalysisTemplate{
        .name = "probe/shape",
        .def = {.label = "Shape", .description = "What the analysis was given"},
        .run = [](const tk::AnalysisRequest& req) {
          if (req.options.value("fail", false)) throw std::invalid_argument("asked to fail");
          json blocks = json::array();
          for (const auto& b : req.blocks) blocks.push_back({{"id", b.id}, {"type", b.def.type}, {"k", tk::get_or(b.params, "k")}});
          json wires = json::array();
          for (const auto& [to, from] : req.wires) wires.push_back({from.first, to.first});
          return json{{"blocks", blocks}, {"wires", wires}, {"inputs", req.inputs}, {"outputs", req.outputs}, {"dt", req.dt}};
        }});
    return out;
  }();
  return r;
}

const tk::Catalog& catalog() {
  static const tk::Catalog c = tk::load_descriptor("domain = \"d\"\nimport = [\"math\", \"probe\"]\n", registry());
  return c;
}

}  // namespace

TEST_CASE("the catalog lists the analyses of the packages it imports") {
  REQUIRE(catalog().analyses.size() == 1);
  const json msg = json::parse(tk::catalog_message(catalog()));
  CHECK(msg["analyses"] == json::array({{{"name", "probe/shape"}, {"label", "Shape"}, {"group", "probe"}, {"description", "What the analysis was given"}}}));
  const auto without = tk::load_descriptor("domain = \"d\"\nimport = [\"math\"]\n", registry());
  CHECK(without.analyses.empty());
  CHECK_FALSE(json::parse(tk::catalog_message(without)).contains("analyses"));
  tk::Registry twice = registry();
  CHECK_THROWS(twice.add(tk::AnalysisTemplate{.name = "probe/shape", .def = {}, .run = {}}));
}

TEST_CASE("a session answers analysis.request with the part of the graph, variables resolved") {
  static const core::Backend backend{.engine = "test", .catalog = &catalog(), .run = {.dt = 0.02, .real_time = false}};
  std::vector<json> sent;
  std::mutex mutex;
  core::Session session(backend, [&](const std::string& text) {
    std::lock_guard lock(mutex);
    sent.push_back(json::parse(text));
  });
  const json graph = {{"nodes", {{{"id", "a"}, {"type", "gain"}, {"params", {{"k", {{"var", "kp"}}}}}},
                                 {{"id", "b"}, {"type", "gain"}, {"params", {{"k", 3}}}}}},
                      {"edges", {{{"from", {"a", "y"}}, {"to", {"b", "u"}}}}}};
  json request = {{"type", "analysis.request"}, {"id", "r1"}, {"analysis", "probe/shape"}, {"graph", graph},
                  {"inputs", json::array({json::array({"a", "u"})})}, {"outputs", json::array({json::array({"b", "y"})})},
                  {"variables", {{{"name", "kp"}, {"type", "number"}, {"value", 2}}}}};
  session.on_message(request.dump());
  json answer = sent.back();
  CHECK(answer["type"] == "analysis.result");
  CHECK(answer["id"] == "r1");
  CHECK_FALSE(answer.contains("error"));
  CHECK(answer["result"]["blocks"] == json::array({{{"id", "a"}, {"type", "gain"}, {"k", 2.0}}, {{"id", "b"}, {"type", "gain"}, {"k", 3.0}}}));
  CHECK(answer["result"]["wires"] == json::array({json::array({"a", "b"})}));
  CHECK(answer["result"]["inputs"] == json::array({json::array({"a", "u"})}));
  CHECK(answer["result"]["dt"] == 0.02);

  request["options"] = {{"fail", true}};
  session.on_message(request.dump());
  CHECK(sent.back()["error"] == "asked to fail");

  request["analysis"] = "probe/nope";
  session.on_message(request.dump());
  CHECK_THAT(sent.back()["error"].get<std::string>(), ContainsSubstring("no analysis \"probe/nope\""));
}
