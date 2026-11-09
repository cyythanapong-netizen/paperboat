// Lists on the wires (#24): "numbers" (a list of numbers) and "points" (a list of points) as
// port types, their wire rules, the list package's blocks, elementwise math as a map, and how
// the Dashboard's monitors show a list.

#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/graph.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

const tk::Catalog& catalog() {
  static const tk::Catalog c =
      tk::load_descriptor("domain = \"l\"\nimport = [\"base\", \"list\", \"math\", \"control\", \"signal\", \"dashboard\"]\n");
  return c;
}

struct Out {
  std::vector<json> messages;
  core::Emit emit() {
    return [this](const json& m) { messages.push_back(m); };
  }
  json last(const std::string& node) const {
    json v;
    for (const auto& m : messages)
      if (m["type"] == "signal" && m["node"] == node) v = m;
    return v;
  }
  std::vector<json> all(const std::string& node) const {
    std::vector<json> out;
    for (const auto& m : messages)
      if (m["type"] == "signal" && m["node"] == node) out.push_back(m);
    return out;
  }
  std::string error() const {
    for (const auto& m : messages)
      if (m["type"] == "log" && m["level"] == "error") return m["text"];
    return {};
  }
};

json node(const std::string& id, const std::string& type, json params = json::object()) {
  return {{"id", id}, {"type", type}, {"params", std::move(params)}};
}
json wire(const std::string& a, const std::string& ap, const std::string& b, const std::string& bp) {
  return {{"from", {a, ap}}, {"to", {b, bp}}};
}

std::string run(const json& graph, Out& out, core::RunSettings::Mode mode = core::RunSettings::Mode::Once,
                double duration = 1, const tk::Catalog& cat = catalog()) {
  const core::Backend backend{.engine = "test", .catalog = &cat,
                              .run = {.mode = mode, .dt = 0.01, .duration = duration, .emit_every = 1, .real_time = false}};
  return core::run("r", graph, nullptr, backend, out.emit(), nullptr, {});
}

// A block of the catalog made with these params and text, in its form.
std::unique_ptr<tk::Block> make(const std::string& type, tk::Values params = {}, tk::Config text = {}) {
  const auto* entry = catalog().find(type);
  REQUIRE(entry != nullptr);
  for (const auto& p : entry->def.params) {
    if (p.is_number() && !params.contains(p.id)) params[p.id] = p.default_value;
    if (!p.is_number() && !text.contains(p.id)) text[p.id] = p.default_text;
  }
  return entry->make(params, text);
}

tk::Vector vec(const tk::Value& v) { return std::get<tk::Vector>(v); }
tk::Table table(const tk::Value& v) { return std::get<tk::Table>(v); }

std::string why(const tk::PortDef& from, const tk::PortDef& to) { return tk::wire_mismatch(from, to).value_or("ok"); }

}  // namespace

TEST_CASE("list ports: numbers join numbers and vectors, points join points, neither a plain signal") {
  const auto numbers = tk::numbers_port("y");
  const auto xz = tk::points_port("p", {{"x", "m"}, {"z", "m"}});
  CHECK(numbers.type == "numbers");
  CHECK(tk::is_list_type("numbers"));
  CHECK(tk::is_list_type("points"));
  CHECK_FALSE(tk::is_list_type("vector"));
  CHECK(tk::vector_dimension(xz) == 2);  // a point's coordinates, not the list's length

  CHECK(why(numbers, tk::numbers_port("u")) == "ok");
  CHECK(why(numbers, tk::vector_port("u")) == "ok");  // a vector is a list of fixed length
  CHECK(why(tk::vector_port("v", 3), numbers) == "ok");
  CHECK(why(numbers, {.id = "u"}) == "joins a numbers port to a signal port");
  CHECK(why({.id = "y"}, numbers) == "joins a signal port to a numbers port");
  CHECK(why(numbers, {.id = "u", .broadcast = true}) == "ok");  // into an elementwise block: a map
  CHECK(why(numbers, {.id = "u", .type = "any"}) == "ok");
  CHECK(why(numbers, {.id = "in", .type = "step"}) == "joins a numbers port to a step port");
  // A list's unit is every item's.
  CHECK(why(tk::numbers_port("y", "m"), tk::numbers_port("u", "m")) == "ok");
  CHECK(why(tk::numbers_port("y", "m"), tk::numbers_port("u", "rad")) == "carries m into a port in rad");
  CHECK(why(tk::numbers_port("y", "m"), {.id = "u", .unit = "s", .broadcast = true}) == "carries m into a port in s");
  CHECK(why(tk::numbers_port("y", "m"), tk::numbers_port("u")) == "ok");

  CHECK(why(xz, tk::points_port("q")) == "ok");
  CHECK(why(xz, tk::points_port("q", {{"a"}, {"b"}})) == "ok");  // names are labels
  CHECK(why(xz, tk::points_port("q", {{"x"}, {"y"}, {"z"}})) == "carries points of 2 coordinates into a port expecting 3");
  CHECK(why(xz, tk::points_port("q", {{"x", "m"}, {"z", "cm"}})) == "carries point coordinate 1 in m into a port in cm");
  CHECK(why(xz, numbers) == "joins a points port to a numbers port");
  CHECK(why(xz, tk::vector_port("v", 2)) == "joins a points port to a vector port");
  CHECK(why(xz, {.id = "u", .broadcast = true}) == "joins a points port to a signal port");
  CHECK(why(xz, {.id = "in", .type = "any"}) == "ok");

  // At run time a points port takes a table whose points have its coordinates (or no points).
  CHECK_FALSE(tk::vector_value_mismatch(xz, tk::Value{tk::Table{{1, 2}, {3, 4}}}));
  CHECK_FALSE(tk::vector_value_mismatch(xz, tk::Value{tk::Vector{}}));
  CHECK_FALSE(tk::vector_value_mismatch(tk::points_port("q"), tk::Value{tk::Table{{1}, {2, 3}}}));
  CHECK(tk::vector_value_mismatch(xz, tk::Value{tk::Table{{1, 2}, {3}}}) == "has point 1 of 1 coordinates, expected 2");
  CHECK(tk::vector_value_mismatch(xz, tk::Value{tk::Vector{1, 2}}) == "expects a list of points");
  CHECK(tk::vector_value_mismatch(xz, tk::Value{1.0}) == "expects a list of points");
  CHECK_FALSE(tk::vector_value_mismatch(numbers, tk::Value{tk::Vector{1, 2, 3, 4, 5}}));
}

TEST_CASE("Range and Series make lists; a bad count fails on the block") {
  const tk::Context ctx;
  const auto tenths = vec(make("range")->step(ctx, {}).at("y"));  // 0 to 1, 11 of them
  REQUIRE(tenths.size() == 11);
  for (std::size_t i = 0; i < tenths.size(); ++i) CHECK_THAT(tenths[i], WithinAbs(0.1 * static_cast<double>(i), 1e-12));
  CHECK(tenths.back() == 1.0);
  CHECK(vec(make("range", {{"start", 2}, {"stop", 4}, {"count", 3}})->step(ctx, {}).at("y")) == tk::Vector{2, 3, 4});
  CHECK(vec(make("range", {{"start", 2}, {"stop", 4}, {"count", 1}})->step(ctx, {}).at("y")) == tk::Vector{2});
  CHECK(vec(make("range", {{"count", 0}})->step(ctx, {}).at("y")) == tk::Vector{});
  CHECK(vec(make("series", {{"start", 1}, {"step", -0.5}, {"count", 4}})->step(ctx, {}).at("y")) == tk::Vector{1, 0.5, 0, -0.5});
  CHECK(catalog().find("range")->def.outputs[0].type == "numbers");
  CHECK(catalog().find("range")->def.kind == tk::Kind::Source);
  CHECK_THROWS_WITH(make("range", {{"count", 2.5}}), ContainsSubstring("count must be a whole number"));
  CHECK_THROWS_WITH(make("series", {{"count", -1}}), ContainsSubstring("count must be a whole number"));

  Out out;  // in a run, the block is named
  CHECK(run({{"nodes", {node("r", "range", {{"count", 1e9}})}}, {"edges", json::array()}}, out) == "failed");
  CHECK_THAT(out.error(), ContainsSubstring("r: count must be a whole number from 0 to 1000000"));
}

TEST_CASE("List Length, Item and Aggregate on numbers, and on points in their points form") {
  const tk::Context ctx;
  const tk::Values list = {{"list", tk::Vector{3, -1, 4, 1, 5}}};
  CHECK(make("list_length")->step(ctx, list).at("n") == 5.0);
  CHECK(make("list_length")->step(ctx, {}).at("n") == 0.0);  // unwired: empty
  CHECK(make("item")->step(ctx, list).at("y") == 3.0);
  CHECK(make("item", {{"index", -1}})->step(ctx, list).at("y") == 5.0);
  CHECK(make("item", {{"index", 2}})->step(ctx, list).at("y") == 4.0);
  auto with_i = list;
  with_i["i"] = 1.0;  // a wired index wins over the param
  CHECK(make("item", {{"index", 2}})->step(ctx, with_i).at("y") == -1.0);
  CHECK_THROWS_WITH(make("item", {{"index", 5}})->step(ctx, list), "index 5 is outside a list of 5 items (0 to 4, or -1 for the last)");
  CHECK_THROWS_WITH(make("item", {{"index", -6}})->step(ctx, list), ContainsSubstring("outside a list of 5"));
  CHECK_THROWS_WITH(make("item", {{"index", 0.5}})->step(ctx, list), ContainsSubstring("whole number"));
  CHECK_THROWS_WITH(make("item")->step(ctx, {}), ContainsSubstring("the list is empty"));

  const auto reduce = [&](const char* op, const tk::Values& in) { return make("aggregate", {}, {{"op", op}})->step(ctx, in).at("y"); };
  CHECK(reduce("sum", list) == 12.0);
  CHECK(reduce("mean", list) == 2.4);
  CHECK(reduce("min", list) == -1.0);
  CHECK(reduce("max", list) == 5.0);
  CHECK(reduce("sum", {}) == 0.0);
  CHECK_THROWS_WITH(reduce("mean", {}), "the mean of an empty list is not defined");
  CHECK_THROWS_WITH(reduce("max", {{"list", tk::Vector{}}}), "the max of an empty list is not defined");

  // The points form: its ports are points in, and a point (a vector) out.
  const auto* item = catalog().find("item");
  const auto points_item = tk::in_form(item->def, "points");
  CHECK(points_item.inputs[0].type == "points");
  CHECK(points_item.outputs[0].type == "vector");
  CHECK(tk::in_form(item->def, "numbers").inputs[0].type == "numbers");
  const tk::Values square = {{"list", tk::Table{{0, 0}, {2, 0}, {2, 1}, {0, 1}}}};
  const tk::Config points = {{"form", "points"}};
  CHECK(make("list_length", {}, points)->step(ctx, square).at("n") == 4.0);
  CHECK(vec(make("item", {{"index", -2}}, points)->step(ctx, square).at("y")) == tk::Vector{2, 1});
  const auto reduce_points = [&](const char* op) {
    return make("aggregate", {}, {{"form", "points"}, {"op", op}})->step(ctx, square).at("y");
  };
  CHECK(vec(reduce_points("mean")) == tk::Vector{1, 0.5});  // the centroid
  CHECK(vec(reduce_points("min")) == tk::Vector{0, 0});     // the bounding box
  CHECK(vec(reduce_points("max")) == tk::Vector{2, 1});
  CHECK(vec(reduce_points("sum")) == tk::Vector{4, 2});
  CHECK_THROWS_WITH(make("aggregate", {}, {{"form", "points"}, {"op", "mean"}})->step(ctx, {}),
                    "the mean of an empty list is not defined");
}

TEST_CASE("Points and Coordinates join lists into points and split them, ports from names") {
  const auto& points = catalog().find("points")->def;
  const auto def = tk::with_dynamic_ports(points, {}, {{"names", "x z"}, {"units", "m m"}});
  REQUIRE(def.inputs.size() == 2);
  CHECK(def.inputs[1].id == "z");
  CHECK(def.inputs[1].type == "numbers");
  CHECK(def.inputs[1].unit == "m");
  CHECK(def.outputs[0].type == "points");
  CHECK(def.outputs[0].dimension == 2u);
  CHECK(def.outputs[0].components[1].id == "z");
  CHECK(def.outputs[0].components[1].unit == "m");

  const auto& coords = catalog().find("coordinates")->def;
  const auto split = tk::with_dynamic_ports(coords, {}, {{"names", "x y z"}, {"units", ""}});
  CHECK(split.outputs.size() == 3);
  CHECK(split.inputs[0].dimension == 3u);
  CHECK(why(def.outputs[0], split.inputs[0]) == "carries points of 2 coordinates into a port expecting 3");

  const tk::Context ctx;
  const auto zipped = make("points", {}, {{"names", "x z"}})->step(ctx, {{"x", tk::Vector{0, 1, 2}}, {"z", tk::Vector{5, 6, 7}}});
  CHECK(table(zipped.at("points")) == tk::Table{{0, 5}, {1, 6}, {2, 7}});
  // An unwired coordinate is 0; no lists in, no points.
  CHECK(table(make("points")->step(ctx, {{"y", tk::Vector{1, 2}}}).at("points")) == tk::Table{{0, 1}, {0, 2}});
  CHECK(table(make("points")->step(ctx, {}).at("points")) == tk::Table{});
  CHECK_THROWS_WITH(make("points")->step(ctx, {{"x", tk::Vector{1, 2}}, {"y", tk::Vector{1}}}),
                    "input y has 1 items, but x has 2: the lists into Points are one length");
  const auto unzipped = make("coordinates", {}, {{"names", "x z"}})->step(ctx, {{"points", tk::Table{{0, 5}, {1, 6}}}});
  CHECK(vec(unzipped.at("x")) == tk::Vector{0, 1});
  CHECK(vec(unzipped.at("z")) == tk::Vector{5, 6});
  CHECK(vec(make("coordinates")->step(ctx, {}).at("y")) == tk::Vector{});
  CHECK_THROWS_WITH(make("points", {}, {{"names", "x points"}}), ContainsSubstring("other port"));
}

TEST_CASE("elementwise math maps over a list; Aggregate collects it; Display and Graph show it") {
  // range 0..4 → x² (f(t) on its input x) → gain 2 → sum: 2 · (0 + 1 + 4 + 9 + 16) = 60
  const json g = {{"nodes",
                   {node("r", "range", {{"start", 0}, {"stop", 4}, {"count", 5}}), node("sq", "function", {{"f", "x^2"}}),
                    node("k", "gain", {{"k", 2}}), node("s", "aggregate", {{"op", "sum"}}), node("n", "list_length"),
                    node("show", "display"), node("list", "display"), node("plot", "graph")}},
                  {"edges",
                   {wire("r", "y", "sq", "x"), wire("sq", "y", "k", "u"), wire("k", "y", "s", "list"), wire("k", "y", "n", "list"),
                    wire("s", "y", "show", "in"), wire("k", "y", "list", "in"), wire("k", "y", "plot", "in")}}};
  Out out;
  REQUIRE(run(g, out) == "done");
  CHECK(out.last("show")["v"] == 60.0);
  CHECK(out.last("list")["v"] == json::array({0.0, 2.0, 8.0, 18.0, 32.0}));  // a Display gets the whole list
  const auto plotted = out.all("plot");  // a Graph in a run-once: y over the index
  REQUIRE(plotted.size() == 5);
  CHECK(plotted[3]["t"] == 3);
  CHECK(plotted[3]["v"] == 18.0);

  // The same in a clock run.
  Out clocked;
  REQUIRE(run(g, clocked, core::RunSettings::Mode::Clock, 0.03) == "done");
  CHECK(clocked.last("show")["v"] == 60.0);

  // A list wire into a plain signal port is refused before the run.
  Out refused;
  json bad = g;
  bad["nodes"].push_back(node("no", "not"));
  bad["edges"].push_back(wire("r", "y", "no", "u"));
  CHECK(run(bad, refused) == "failed");
  CHECK_THAT(refused.error(), ContainsSubstring("joins a numbers port to a signal port"));
  // Into a fixed-length vector port, a list is checked when it arrives: a Demux of 2 gets 5.
  Out sized;
  json demuxed = g;
  demuxed["nodes"].push_back(node("m", "demux"));
  demuxed["edges"].push_back(wire("r", "y", "m", "u"));
  CHECK(run(demuxed, sized) == "failed");
  CHECK_THAT(sized.error(), ContainsSubstring("has 5 values, expected 2"));
}

TEST_CASE("a list of points on the Dashboard: a Display counts it, a Graph in a run-once draws y over x") {
  const json g = {{"nodes",
                   {node("x", "range", {{"start", -1}, {"stop", 1}, {"count", 5}}), node("y", "function", {{"f", "x^2"}}),
                    node("p", "points"), node("c", "list_length", {{"form", "points"}}), node("mid", "aggregate", {{"form", "points"}, {"op", "mean"}}),
                    node("show", "display"), node("count", "display"), node("centre", "display"), node("plot", "graph")}},
                  {"edges",
                   {wire("x", "y", "y", "x"), wire("x", "y", "p", "x"), wire("y", "y", "p", "y"), wire("p", "points", "c", "list"),
                    wire("p", "points", "mid", "list"), wire("p", "points", "show", "in"), wire("c", "n", "count", "in"),
                    wire("mid", "y", "centre", "in"), wire("p", "points", "plot", "in")}}};
  Out out;
  REQUIRE(run(g, out) == "done");
  CHECK(out.last("show")["v"] == "5 points: (-1, 1), (-0.5, 0.25), (0, 0), (0.5, 0.25), …");
  CHECK(out.last("count")["v"] == 5.0);
  CHECK_THAT(out.last("centre")["v"][0].get<double>(), WithinAbs(0, 1e-12));
  CHECK_THAT(out.last("centre")["v"][1].get<double>(), WithinAbs(0.5, 1e-12));
  const auto plotted = out.all("plot");
  REQUIRE(plotted.size() == 5);
  CHECK(plotted[0]["t"] == -1.0);
  CHECK(plotted[0]["v"] == 1.0);
  CHECK(plotted[2]["t"] == 0.0);
  CHECK(plotted[2]["v"] == 0.0);

  // A points wire into a numbers port is refused when the graph is checked.
  Out refused;
  json bad = g;
  bad["edges"].push_back(wire("p", "points", "c2", "list"));
  bad["nodes"].push_back(node("c2", "list_length"));
  CHECK(run(bad, refused) == "failed");
  CHECK_THAT(refused.error(), ContainsSubstring("joins a points port to a numbers port"));
}

TEST_CASE("an elementwise block on a list that changes length: stateless lanes follow, stateful ones fail") {
  const auto lanes = [](const std::string& type, tk::Values params) {
    const auto* entry = catalog().find(type);
    return tk::make_lanes([entry](const tk::Values& p) { return entry->make(p, {}); }, std::move(params), 0);
  };
  const tk::Context ctx{.dt = 0.1};
  auto gain = lanes("gain", {{"k", 3}});
  CHECK(vec(gain->step(ctx, {{"u", tk::Vector{1, 2}}}).at("y")) == tk::Vector{3, 6});
  CHECK(vec(gain->step(ctx, {{"u", tk::Vector{1, 2, 3}}}).at("y")) == tk::Vector{3, 6, 9});
  CHECK(vec(gain->step(ctx, {{"u", tk::Vector{}}}).at("y")) == tk::Vector{});  // no items in, none out
  CHECK(vec(gain->step(ctx, {{"u", tk::Vector{4}}}).at("y")) == tk::Vector{12});

  auto pid = lanes("pid", {{"kp", 1}, {"ki", 1}, {"kd", 0}});
  CHECK_NOTHROW(pid->step(ctx, {{"e", tk::Vector{1, 1}}}));
  CHECK_THROWS_WITH(pid->step(ctx, {{"e", tk::Vector{1, 1, 1}}}),
                    ContainsSubstring("input e has 3 values, but had 2: this block keeps a state per element"));
}

TEST_CASE("a descriptor uses list templates with units and runs a graph of lists") {
  const tk::Catalog cat = tk::load_descriptor(R"(domain = "parametric"
import = ["list", "math", "dashboard"]

[[blocks]]
use = "list/range"
type = "stations"
label = "Stations"
units = { y = "m" }
defaults = { start = 0, stop = 10, count = 3 }

[[blocks]]
use = "list/points"
type = "profile"
defaults = { names = "x z", units = "m m" }

[graph]
nodes = [{ id = "s", type = "stations" }, { id = "p", type = "profile" },
         { id = "n", type = "list_length", params = { form = "points" } }, { id = "d", type = "display" }]
edges = [{ from = ["s", "y"], to = ["p", "x"] }, { from = ["s", "y"], to = ["p", "z"] },
         { from = ["p", "points"], to = ["n", "list"] }, { from = ["n", "n"], to = ["d", "in"] }]
)");
  const json blocks = json::parse(tk::catalog_message(cat))["blocks"];
  const auto block = [&](const std::string& type) {
    for (const auto& b : blocks)
      if (b["type"] == type) return b;
    return json();
  };
  CHECK(block("stations")["outputs"][0] == json({{"id", "y"}, {"type", "numbers"}, {"unit", "m"}}));
  CHECK(block("profile")["outputs"][0]["type"] == "points");
  CHECK(block("profile")["outputs"][0]["dimension"] == 2);
  CHECK(block("item")["params"][0]["options"] == json::array({"numbers", "points"}));  // the form

  Out out;
  REQUIRE(run(json::parse(cat.graph), out, core::RunSettings::Mode::Once, 1, cat) == "done");
  CHECK(out.last("d")["v"] == 3.0);

  // Units go along a list wire: metres into a port in radians are refused.
  const tk::Catalog wrong = tk::load_descriptor(R"(domain = "parametric"
import = ["list", "dashboard"]
[[blocks]]
use = "list/range"
type = "angles"
units = { y = "rad" }
[[blocks]]
use = "list/points"
type = "profile"
defaults = { units = "m" }
)");
  Out refused;
  CHECK(run({{"nodes", {node("a", "angles"), node("p", "profile")}}, {"edges", {wire("a", "y", "p", "x")}}}, refused,
            core::RunSettings::Mode::Once, 1, wrong) == "failed");
  CHECK_THAT(refused.error(), ContainsSubstring("carries rad into a port in m"));
}
