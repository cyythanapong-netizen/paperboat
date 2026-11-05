// Connection rules (#4): a wire joins ports of one type ("any" takes any value, never a
// step), in one unit when both ends have one; an input takes one wire. Units come from
// templates or a descriptor's `units`, go out in the catalog, and bound variables keep them.

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/core/graph.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/variables.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using json = nlohmann::json;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("wire_mismatch: types, any, steps and units") {
  auto why = [](tk::PortDef from, tk::PortDef to) { return tk::wire_mismatch(from, to).value_or("ok"); };
  CHECK(why({"y"}, {"u"}) == "ok");
  CHECK(why({"y", "image"}, {"u", "image"}) == "ok");
  CHECK(why({"y", "signal"}, {"u", "image"}) == "joins a signal port to an image port");
  CHECK(why({"y", "any"}, {"u", "image"}) == "ok");  // any gives any value
  CHECK(why({"y", "points"}, {"u", "any"}) == "ok");  // any takes any value
  CHECK(why({"next", "step"}, {"in", "step"}) == "ok");
  CHECK(why({"next", "step"}, {"u", "any"}) == "joins a step port to an any port");  // never a step
  CHECK(why({"y", "any"}, {"in", "step"}) == "joins an any port to a step port");
  CHECK(why({"x", "signal", "m"}, {"u", "signal", "m"}) == "ok");
  CHECK(why({"x", "signal", "m"}, {"force", "signal", "N"}) == "carries m into a port in N");
  CHECK(why({"x", "signal", "m"}, {"u"}) == "ok");  // a port without a unit takes any
  CHECK(why({"y"}, {"force", "signal", "N"}) == "ok");
}

TEST_CASE("vector ports keep one wire and check declared shape and component units") {
  auto why = [](tk::PortDef from, tk::PortDef to) { return tk::wire_mismatch(from, to).value_or("ok"); };
  const auto state = tk::vector_port("state", std::vector<tk::VectorComponent>{{"x", "m"}, {"angle", "rad"}});
  const tk::PortDef same_size{.id = "state", .type = "numbers", .dimension = 2,
                              .components = {{"position", "m"}, {"heading", "rad"}}};
  CHECK(state.type == "vector");
  CHECK(tk::vector_dimension(state) == 2);
  CHECK(why(state, same_size) == "ok");  // names don't constrain compatibility; units are positional
  CHECK(why(tk::vector_port("v", 2), tk::vector_port("v", 3)) ==
        "carries a vector of 2 values into a port expecting 3");
  const auto wrong_unit = tk::vector_port("v", std::vector<tk::VectorComponent>{{"x", "cm"}, {"angle", "rad"}});
  CHECK(why(state, wrong_unit) == "carries vector component 0 in m into a port in cm");
  CHECK(why(state, {"v", "any"}) == "ok");

  CHECK_FALSE(tk::vector_value_mismatch(state, tk::Value{tk::Vector{1, 2}}));
  CHECK(tk::vector_value_mismatch(state, tk::Value{tk::Vector{1}}) == "has 1 values, expected 2");
  CHECK(tk::vector_value_mismatch(state, tk::Value{3.0}) == "expects a vector value");
}

namespace {

const tk::Catalog& catalog() {
  static const tk::Catalog c = tk::load_descriptor(R"(domain = "c"
import = ["base", "math", "control", "dashboard"]

[[blocks]]
use = "math/gain"
type = "push"
units = { u = "m", y = "N", k = "N/m" }
)");
  return c;
}

std::string graph_fails(const json& nodes, const json& edges) {
  core::Station station;
  try {
    core::prepare_graph({{"nodes", nodes}, {"edges", edges}}, catalog(), station);
  } catch (const core::GraphError& err) {
    return err.node + ": " + err.what();
  }
  return "ok";
}

json node(const char* id, const char* type) { return {{"id", id}, {"type", type}}; }
json wire(const char* a, const char* ap, const char* b, const char* bp) { return {{"from", {a, ap}}, {"to", {b, bp}}}; }

}  // namespace

TEST_CASE("a graph's wires follow the rules: one per input, matching types and units") {
  const json nodes = {node("one", "constant"), node("two", "constant"), node("p", "push"), node("q", "push"), node("show", "display")};
  CHECK(graph_fails(nodes, {wire("one", "y", "p", "u"), wire("p", "y", "show", "in")}) == "ok");
  CHECK_THAT(graph_fails(nodes, {wire("one", "y", "p", "u"), wire("two", "y", "p", "u")}),
             ContainsSubstring("p: input p.u has two wires; an input takes one"));
  CHECK_THAT(graph_fails(nodes, json::array({wire("p", "y", "q", "u")})), ContainsSubstring("q: wire p.y -> q.u carries N into a port in m"));
  CHECK(graph_fails(nodes, json::array({wire("p", "y", "show", "in")})) == "ok");  // a monitor takes any value, any unit
}

TEST_CASE("units from a descriptor go out in the catalog; bad ones are refused") {
  const json msg = json::parse(tk::catalog_message(catalog()));
  json push;
  for (const auto& b : msg["blocks"])
    if (b["type"] == "push") push = b;
  REQUIRE(push.is_object());
  CHECK(push["inputs"][0] == json{{"id", "u"}, {"type", "signal"}, {"unit", "m"}});
  CHECK(push["outputs"][0]["unit"] == "N");
  CHECK(push["params"][0]["unit"] == "N/m");
  auto bad = [](const char* units) -> std::string {
    try {
      tk::load_descriptor(std::string("domain = \"c\"\nimport = [\"math\", \"base\"]\n[[blocks]]\nuse = \"") + units);
    } catch (const tk::DescriptorError& err) {
      return err.what();
    }
    return "ok";
  };
  CHECK_THAT(bad("math/gain\"\nunits = { nope = \"N\" }\n"), ContainsSubstring("has no port or param \"nope\""));
  CHECK_THAT(bad("math/gain\"\nunits = { u = 3 }\n"), ContainsSubstring("units.u is text"));
  CHECK_THAT(bad("base/text\"\nunits = { value = \"m\" }\n"), ContainsSubstring("only numbers have units"));
}

TEST_CASE("a variable bound to a param with a unit must be in that unit (#36)") {
  const tk::Variables vars(json::array({{{"name", "stiff"}, {"type", "number"}, {"value", 40}, {"unit", "N/m"}},
                                        {{"name", "angle"}, {"type", "number"}, {"value", 1}, {"unit", "rad"}},
                                        {{"name", "plain"}, {"type", "number"}, {"value", 2}}}));
  const tk::ParamDef k{.id = "k", .default_value = 1, .unit = "N/m"};
  CHECK(vars.value_for({{"var", "stiff"}}, k, "p") == 40.0);
  CHECK(vars.value_for({{"var", "plain"}}, k, "p") == 2.0);  // no unit: fits any
  CHECK_THROWS_WITH(vars.value_for({{"var", "angle"}}, k, "p"), ContainsSubstring("p.k is in N/m, but angle is in rad"));
}
