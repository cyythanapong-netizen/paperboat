// Variables (#36): checked and resolved, referenced from params and props, in a descriptor.

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/variables.hpp"

namespace tk = paperboat::toolkit;
using json = nlohmann::json;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

json number(const char* name, double value, json extra = json::object()) {
  json v = {{"name", name}, {"type", "number"}, {"value", value}};
  v.update(extra);
  return v;
}
json formula(const char* name, const char* expr, const char* type = "number") {
  return {{"name", name}, {"type", type}, {"expr", expr}};
}

// The message a list (or a reference) fails with.
std::string fails(std::initializer_list<json> items) {
  try {
    json list = json::array();
    for (const auto& item : items) list.push_back(item);
    tk::Variables{list};
  } catch (const tk::VariableError& err) {
    return err.what();
  }
  return "(no error)";
}

const tk::ParamDef kNumber{.id = "kp", .default_value = 1};
const tk::ParamDef kBoolean{.id = "on", .default_value = 0, .type = "boolean"};
const tk::ParamDef kText{.id = "label", .type = "text"};
const tk::ParamDef kEnum{.id = "op", .type = "enum", .default_text = ">", .options = {">", "<"}};
const tk::ParamDef kKey{.id = "key", .type = "key", .default_text = "Space"};

}  // namespace

TEST_CASE("variables resolve: literals, and formulas in dependency order") {
  const tk::Variables vars(json::array({
      formula("kd", "2 * sqrt(kp * len)"),  // before what it uses: order doesn't matter
      number("kp", 40),
      number("len", 1),
      formula("stiff", "kp > 10", "boolean"),
      {{"name", "controller"}, {"type", "enum"}, {"value", "pid"}, {"options", {"pid", "lqr"}}},
      {{"name", "title"}, {"type", "text"}, {"value", "Pendulum"}},
  }));
  const json v = vars.values();
  CHECK_THAT(v["kd"].get<double>(), WithinAbs(12.649, 1e-3));
  CHECK(v["stiff"] == true);
  CHECK(v["controller"] == "pid");
  CHECK(v["title"] == "Pendulum");
  CHECK(tk::Variables(json()).empty());  // none at all
}

TEST_CASE("a variable list that doesn't hold says why, in words") {
  CHECK_THAT(fails({number("2fast", 1)}), ContainsSubstring("can't be a variable's name"));
  CHECK_THAT(fails({number("sin", 1)}), ContainsSubstring("can't be a variable's name"));
  CHECK_THAT(fails({number("t", 1)}), ContainsSubstring("can't be a variable's name"));
  CHECK_THAT(fails({number("a", 1), number("a", 2)}), ContainsSubstring("two variables are called a"));
  CHECK_THAT(fails({{{"name", "a"}, {"type", "colour"}, {"value", 1}}}), ContainsSubstring("type must be"));
  CHECK_THAT(fails({{{"name", "a"}, {"type", "number"}, {"value", "1"}}}), ContainsSubstring("a is a number, but its value is text"));
  CHECK_THAT(fails({{{"name", "a"}, {"type", "number"}}}), ContainsSubstring("a value or a formula"));
  CHECK_THAT(fails({{{"name", "m"}, {"type", "enum"}, {"value", "x"}, {"options", {"pid"}}}}), ContainsSubstring("not one of its options"));
  CHECK_THAT(fails({formula("a", "b + 1"), formula("b", "a * 2")}), ContainsSubstring("variables loop: a → b → a"));
  CHECK_THAT(fails({number("kp_x", 1), formula("kd", "2 * kpx")}), ContainsSubstring("unknown variable \"kpx\" (kp_x?)"));
  CHECK_THAT(fails({formula("kd", "2 *")}), ContainsSubstring("kd: 2 *"));
  CHECK_THAT(fails({{{"name", "m"}, {"type", "enum"}, {"value", "a"}, {"options", {"a"}}}, formula("x", "m + 1")}),
             ContainsSubstring("formulas take numbers and booleans"));
  CHECK_THAT(fails({{{"name", "s"}, {"type", "text"}, {"expr", "1"}}}), ContainsSubstring("only a number or a boolean can be a formula"));
  CHECK_THAT(fails({number("len", 5, {{"max", 3}})}), ContainsSubstring("len is 5, above its maximum 3"));
  CHECK_THAT(fails({number("n", 2.5, {{"whole", true}})}), ContainsSubstring("not a whole number"));
  CHECK(fails({number("len", 1), formula("short", "len - 2")}) == "(no error)");  // no range: anything goes
  CHECK_THAT(fails({number("len", 1), {{"name", "short"}, {"type", "number"}, {"expr", "len - 2"}, {"min", 0}}}),
             ContainsSubstring("below its minimum 0"));
}

TEST_CASE("a param refers to a variable, or holds a formula; checked against the param") {
  const tk::Variables vars(json::array({number("kp", 40), {{"name", "on"}, {"type", "boolean"}, {"value", true}},
                                        {{"name", "op"}, {"type", "enum"}, {"value", "<"}, {"options", {">", "<", "=="}}},
                                        {{"name", "eq"}, {"type", "enum"}, {"value", "=="}, {"options", {">", "<", "=="}}},
                                        {{"name", "label"}, {"type", "text"}, {"value", "θ"}}}));
  CHECK(vars.value_for(3.5, kNumber, "pid") == 3.5);  // a literal stays
  CHECK(vars.value_for({{"var", "kp"}}, kNumber, "pid") == 40.0);
  CHECK(vars.value_for({{"expr", "kp / 4"}}, kNumber, "pid") == 10.0);
  CHECK(vars.value_for({{"expr", "kp > 50"}}, kBoolean, "pid") == false);
  CHECK(vars.value_for({{"var", "op"}}, kEnum, "cmp") == "<");
  CHECK(vars.value_for({{"var", "label"}}, kText, "plot") == "θ");
  auto fails_for = [&](const json& given, const tk::ParamDef& def) -> std::string {
    try {
      vars.value_for(given, def, "pid");
    } catch (const tk::VariableError& err) {
      CHECK(err.where == "pid");  // the block, so the UI marks it
      return err.what();
    }
    return "(no error)";
  };
  CHECK_THAT(fails_for({{"var", "kq"}}, kNumber), ContainsSubstring("pid.kp: unknown variable \"kq\" (kp?)"));
  CHECK_THAT(fails_for({{"var", "on"}}, kNumber), ContainsSubstring("pid.kp is number, but on is a boolean"));
  CHECK_THAT(fails_for({{"var", "eq"}}, kEnum), ContainsSubstring("eq is \"==\", not one of: >, <"));
  CHECK_THAT(fails_for({{"expr", "kp"}}, kText), ContainsSubstring("only number and boolean params take a formula"));
  CHECK_THAT(fails_for({{"var", "label"}}, kKey), ContainsSubstring("pid.key is key"));
  CHECK_THAT(fails_for({{"var", "kp"}, {"expr", "kp"}}, kNumber), ContainsSubstring("a reference is"));
}

TEST_CASE("[[variables]] in a descriptor: types from values, checked, and the starters use them") {
  const auto catalog = tk::load_descriptor(R"(domain = "v"
import = ["math", "signal"]

[[variables]]
name = "k"
value = 2
unit = "1"

[[variables]]
name = "twice"
expr = "2 * k"

[[variables]]
name = "verbose"
value = false

[graph]
nodes = [{ id = "gain-1", type = "gain", params = { k = { var = "twice" } } }]
edges = []
)");
  const json list = json::parse(catalog.variables);
  REQUIRE(list.size() == 3);
  CHECK(list[0]["type"] == "number");
  CHECK(list[1]["type"] == "number");  // a formula is a number
  CHECK(list[2]["type"] == "boolean");
  const json graph = tk::resolve_graph(json::parse(catalog.graph), catalog, tk::Variables(list));
  CHECK(graph["nodes"][0]["params"]["k"] == 4.0);

  auto bad = [](const char* toml) -> std::string {
    try {
      tk::load_descriptor(toml);
    } catch (const tk::DescriptorError& err) {
      return err.what();
    }
    return "(no error)";
  };
  CHECK_THAT(bad("domain = \"v\"\nimport = [\"math\"]\n[[variables]]\nname = \"k\"\nvalue = 1\ncolour = \"red\"\n"),
             ContainsSubstring("variables[0]"));
  CHECK_THAT(bad("domain = \"v\"\nimport = [\"math\"]\n[[variables]]\nname = \"a\"\nexpr = \"b\"\n"),
             ContainsSubstring("variables: a: unknown variable \"b\""));
  CHECK_THAT(bad("domain = \"v\"\nimport = [\"math\"]\n[graph]\nnodes = [{ id = \"g\", type = \"gain\", params = { k = { var = \"nope\" } } }]\nedges = []\n"),
             ContainsSubstring("starter: g.k: unknown variable \"nope\""));
}
