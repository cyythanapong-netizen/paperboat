// Vectors in flows (#164): Mux and Demux, ports from names, elementwise blocks as lanes,
// the lane count resolved from the wires, a Graph's traces, a vector into a linked block.

#include <fstream>
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
  static const tk::Catalog c = tk::load_descriptor("domain = \"v\"\nimport = [\"base\", \"math\", \"control\", \"signal\", \"string\", \"dashboard\"]\n");
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

std::string run(const json& graph, Out& out, core::RunSettings::Mode mode = core::RunSettings::Mode::Once, double duration = 1,
                const tk::Catalog& cat = catalog(), const json& station = nullptr) {
  const core::Backend backend{.engine = "test", .catalog = &cat,
                              .run = {.mode = mode, .dt = 0.01, .duration = duration, .emit_every = 1, .real_time = false}};
  return core::run("r", graph, station, backend, out.emit(), nullptr, {});
}

std::vector<double> numbers(const json& v) { return v.get<std::vector<double>>(); }

}  // namespace

TEST_CASE("Mux and Demux take their ports from names, with units by position") {
  const auto& mux = catalog().find("mux")->def;
  const auto def = tk::with_dynamic_ports(mux, {}, {{"names", "x, v theta"}, {"units", "m m/s rad"}});
  REQUIRE(def.inputs.size() == 3);
  CHECK(def.inputs[1].id == "v");
  CHECK(def.inputs[1].unit == "m/s");
  CHECK(def.outputs[0].dimension == 3u);
  CHECK(def.outputs[0].components[2].id == "theta");
  CHECK(def.outputs[0].components[2].unit == "rad");

  const auto& demux = catalog().find("demux")->def;
  const auto split = tk::with_dynamic_ports(demux, {}, {{"names", "a b c d"}, {"units", ""}});
  CHECK(split.outputs.size() == 4);
  CHECK(split.inputs[0].dimension == 4u);
  // A wrong-length vector is refused when drawn, by the rule that was already there.
  CHECK(tk::wire_mismatch(def.outputs[0], split.inputs[0]) == "carries a vector of 3 values into a port expecting 4");

  CHECK_THROWS_WITH(tk::parse_names("x x"), ContainsSubstring("twice"));
  CHECK_THROWS_WITH(tk::parse_names(" , "), ContainsSubstring("at least one"));
  CHECK_THROWS_WITH(tk::parse_names("1x"), ContainsSubstring("isn't a name"));
  CHECK_THROWS_WITH(tk::parse_names("a y", {"y"}), ContainsSubstring("other port"));
  // In a run, bad names fail on the block.
  Out out;
  CHECK(run({{"nodes", {node("m", "mux", {{"names", "a a"}})}}, {"edges", json::array()}}, out) == "failed");
  CHECK_THAT(out.error(), ContainsSubstring("m: names: \"a\" is there twice"));
}

TEST_CASE("an elementwise block's signal ports take a vector wire; others still refuse one") {
  const tk::PortDef vec = tk::vector_port("y", 3);
  const auto gain = tk::with_broadcast(catalog().find("gain")->def);
  CHECK(gain.inputs[0].broadcast);
  CHECK_FALSE(tk::wire_mismatch(vec, gain.inputs[0]));
  CHECK_FALSE(tk::wire_mismatch(gain.outputs[0], tk::vector_port("u", 2)));  // a UI can't tell yet: let it draw
  const tk::PortDef scalar{.id = "u"};
  CHECK(tk::wire_mismatch(vec, scalar) == "joins a vector port to a signal port");
  CHECK_FALSE(catalog().find("delay")->def.elementwise);
}

TEST_CASE("Mux, Gain and Demux: a vector scaled as one and split again; scalar flows unchanged") {
  Out out;
  const json g = {{"nodes",
                   {node("x", "constant", {{"value", 1}}), node("v", "constant", {{"value", 2}}),
                    node("th", "constant", {{"value", 3}}), node("m", "mux", {{"names", "x v th"}}),
                    node("k", "gain", {{"k", 2}}), node("d", "demux", {{"names", "x v th"}}),
                    node("all", "display"), node("th2", "display")}},
                  {"edges",
                   {wire("x", "y", "m", "x"), wire("v", "y", "m", "v"), wire("th", "y", "m", "th"), wire("m", "y", "k", "u"),
                    wire("k", "y", "d", "u"), wire("k", "y", "all", "in"), wire("d", "th", "th2", "in")}}};
  REQUIRE(run(g, out) == "done");
  CHECK(numbers(out.last("all")["v"]) == std::vector<double>{2, 4, 6});
  // The vector keeps the names it came in with.
  CHECK(out.last("all")["components"][1]["id"] == "v");
  CHECK(out.last("th2")["v"] == 6.0);
}

TEST_CASE("scalars broadcast into an elementwise block; vectors of two lengths fail") {
  Out out;
  json g = {{"nodes",
             {node("m", "mux"), node("ten", "constant", {{"value", 10}}), node("s", "sum", {{"signs", "+-"}}),
              node("show", "display")}},
            {"edges", {wire("m", "y", "s", "a"), wire("ten", "y", "s", "b"), wire("s", "y", "show", "in")}}};
  REQUIRE(run(g, out) == "done");
  CHECK(numbers(out.last("show")["v"]) == std::vector<double>{-10, -10});  // [0, 0] − 10

  Out bad;
  g["nodes"].push_back(node("m3", "mux", {{"names", "a b c"}}));
  g["edges"][1] = wire("m3", "y", "s", "b");
  CHECK(run(g, bad) == "failed");
  CHECK_THAT(bad.error(), ContainsSubstring("s: input b gets a vector of 3 values, but a gets 2"));
}

TEST_CASE("an Integrator in a vector loop knows its length from the wires: one state per element") {
  // x' = u − x per element, u = [1, 2, 3]: x = u (1 − e^−t).
  Out out;
  const json g = {{"nodes",
                   {node("a", "constant", {{"value", 1}}), node("b", "constant", {{"value", 2}}), node("c", "constant", {{"value", 3}}),
                    node("u", "mux", {{"names", "a b c"}}), node("e", "sum", {{"signs", "+-"}}), node("x", "integrator"),
                    node("show", "display")}},
                  {"edges",
                   {wire("a", "y", "u", "a"), wire("b", "y", "u", "b"), wire("c", "y", "u", "c"), wire("u", "y", "e", "a"),
                    wire("x", "y", "e", "b"), wire("e", "y", "x", "u"), wire("x", "y", "show", "in")}}};
  REQUIRE(run(g, out, core::RunSettings::Mode::Clock, 1.0) == "done");
  const auto x = numbers(out.last("show")["v"]);
  REQUIRE(x.size() == 3);
  for (std::size_t i = 0; i < 3; ++i) CHECK_THAT(x[i], WithinAbs(static_cast<double>(i + 1) * (1 - std::exp(-1.0)), 0.01));
}

TEST_CASE("a vector of a length known only at run time can't feed a block without feedthrough") {
  Out out;
  const json g = {{"nodes", {node("t", "text"), node("s", "split"), node("x", "integrator")}},
                  {"edges", {wire("t", "y", "s", "text"), wire("s", "vector", "x", "u")}}};
  const auto* split = catalog().find("split");
  REQUIRE(split != nullptr);
  CHECK(run(g, out) == "failed");
  CHECK_THAT(out.error(), ContainsSubstring("known only at run time"));
}

TEST_CASE("a Graph in a clock run draws one trace per component; run-once plots by index") {
  const json g = {{"nodes", {node("m", "mux", {{"names", "p q"}}), node("plot", "graph")}}, {"edges", {wire("m", "y", "plot", "in")}}};
  Out clock;
  REQUIRE(run(g, clock, core::RunSettings::Mode::Clock, 0.05) == "done");
  CHECK(clock.last("plot")["v"].is_array());
  CHECK(clock.last("plot")["components"][1]["id"] == "q");
  Out once;
  REQUIRE(run(g, once) == "done");
  CHECK(once.last("plot")["v"].is_number());
  CHECK(once.last("plot")["t"] == 1);  // the second element, at x = 1
}

TEST_CASE("lanes: a param change reaches every lane, a stateless lane is made again") {
  const auto* gain = catalog().find("gain");
  auto lanes = tk::make_lanes([&](const tk::Values& p) { return gain->make(p); }, {{"k", 2.0}}, 2);
  CHECK(std::get<tk::Vector>(lanes->step({}, {{"u", tk::Vector{1, 2}}}).at("y")) == tk::Vector{2, 4});
  CHECK(lanes->set_param("k", 3));
  CHECK(std::get<tk::Vector>(lanes->step({}, {{"u", tk::Vector{1, 2}}}).at("y")) == tk::Vector{3, 6});
  CHECK_THROWS_WITH(lanes->step({}, {{"u", tk::Vector{1, 2, 3}}}), ContainsSubstring("has 3 values, expected 2"));
  CHECK(tk::as_vector({{"u", 5.0}}, "u") == tk::Vector{5});
}

namespace {

// A station object that takes one vector wire: its linked block's input is a vector port.
struct Plant final : tk::StationObject {
  explicit Plant(const tk::Values&) {}
  tk::Values read() const override { return {{"sum", total}}; }
  void step(const tk::Context&, const tk::Values& in) override {
    total = 0;
    for (double v : tk::as_vector(in, "u")) total += v;
  }
  std::vector<tk::Entity> entities(const std::string&, double) const override { return {}; }
  double total = 0;
};

}  // namespace

TEST_CASE("a linked block takes a vector as one wire, from a Mux or an elementwise block") {
  tk::Registry registry = tk::builtins();
  registry.add(tk::define_object<Plant>("plant", {.type = "plant", .label = "Plant",
                                                  .block = tk::BlockDef{.type = "plant", .label = "Plant", .kind = tk::Kind::Source,
                                                                        .inputs = {tk::vector_port("u", 3)}, .outputs = {{"sum"}}}}));
  static const tk::Catalog cat = tk::load_descriptor("domain = \"plant\"\nimport = [\"base\", \"math\", \"dashboard\"]\n", registry);
  const json station = {{"objects", {{{"id", "p"}, {"type", "plant"}}}}};
  const json g = {{"nodes", {node("m", "mux", {{"names", "a b c"}}), node("one", "constant", {{"value", 1}}), node("k", "gain", {{"k", 2}}),
                             node("p", "plant"), node("show", "display")}},
                  {"edges", {wire("one", "y", "m", "a"), wire("one", "y", "m", "c"), wire("m", "y", "k", "u"), wire("k", "y", "p", "u"),
                             wire("p", "sum", "show", "in")}}};
  Out out;
  REQUIRE(run(g, out, core::RunSettings::Mode::Clock, 0.05, cat, station) == "done");
  CHECK(out.last("show")["v"] == 4.0);  // 2 · (1 + 0 + 1)

  Out wrong;  // a Mux of 2 into a port of 3 is refused before the run
  json two = g;
  two["nodes"][0] = node("m", "mux", {{"names", "a b"}});
  two["edges"][1] = wire("one", "y", "m", "b");
  CHECK(run(two, wrong, core::RunSettings::Mode::Clock, 0.05, cat, station) == "failed");
  CHECK_THAT(wrong.error(), ContainsSubstring("carries a vector of 2 values into a port expecting 3"));
}

TEST_CASE("dynamic ports: the engine and the UI agree on the shared fixture") {
  std::ifstream in(std::string(PAPERBOAT_TEST_DATA) + "/ports.json");
  REQUIRE(in);
  const json fixture = json::parse(in);
  const json blocks = json::parse(tk::catalog_message(catalog()))["blocks"];
  for (const auto& c : fixture) {
    INFO(c["type"]);
    const auto it = std::ranges::find_if(blocks, [&](const json& b) { return b["type"] == c["type"]; });
    REQUIRE(it != blocks.end());
    CHECK(*it == c["def"]);  // the fixture's def is what the catalog sends
    const auto* entry = catalog().find(c["type"].get<std::string>());
    tk::Values params;
    tk::Config text;
    for (const auto& p : entry->def.params) {
      if (p.is_number()) params[p.id] = c["params"].value(p.id, p.default_value);
      else text[p.id] = c["params"].value(p.id, p.default_text);
    }
    auto def = tk::with_dynamic_ports(entry->def, params, text);
    if (!entry->def.inputs_from.empty()) def = tk::with_inputs(def, text.at(entry->def.inputs_from));
    std::vector<std::string> ins, outs;
    for (const auto& p : def.inputs) ins.push_back(p.id);
    for (const auto& p : def.outputs) outs.push_back(p.id);
    CHECK(ins == c["inputs"].get<std::vector<std::string>>());
    CHECK(outs == c["outputs"].get<std::vector<std::string>>());
  }
}

TEST_CASE("a Scope sends one sample per wired input, tagged with it; a vector stays whole") {
  const json g = {{"nodes", {node("one", "constant", {{"value", 1}}), node("m", "mux", {{"names", "p q"}}),
                             node("sc", "scope", {{"inputs", 3}})}},
                  {"edges", {wire("one", "y", "sc", "a"), wire("m", "y", "sc", "c")}}};
  for (const auto mode : {core::RunSettings::Mode::Once, core::RunSettings::Mode::Clock}) {
    Out out;
    REQUIRE(run(g, out, mode, 0.05) == "done");
    std::map<std::string, json> by_port;
    for (const auto& m : out.messages)
      if (m["type"] == "signal" && m["node"] == "sc") by_port[m.at("port").get<std::string>()] = m;
    REQUIRE(by_port.size() == 2);  // b is unwired: nothing
    CHECK(by_port["a"]["v"] == 1.0);
    CHECK(by_port["c"]["v"].is_array());
    CHECK(by_port["c"]["components"][1]["id"] == "q");
  }
  // A backend's default layout may carry an instrument's settings (a table; the toolkit's UI reads them).
  CHECK_NOTHROW(tk::load_descriptor("import = [\"dashboard\"]\n[dashboard]\ninstruments = [{ block = \"sc\", settings = { mode = \"xy\", window = 2 } }]\n"));
  CHECK_THROWS_WITH(tk::load_descriptor("import = [\"dashboard\"]\n[dashboard]\ninstruments = [{ block = \"sc\", settings = 3 }]\n"),
                    ContainsSubstring("settings must be a table"));
}
