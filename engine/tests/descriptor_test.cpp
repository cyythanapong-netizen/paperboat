#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/installer.hpp"
#include "rig.hpp"

namespace tk = paperboat::toolkit;
using Catch::Matchers::ContainsSubstring;

namespace {

// Puts [[blocks]] entries under a minimal descriptor header.
std::string descriptor(const std::string& blocks) {
  return "domain = \"test\"\n" + blocks;
}

// The "test" domain's own blocks, defined in code as a domain does (GUIDELINES §6).
struct Motor final : tk::Block {
  explicit Motor(const tk::Values& params) : max(tk::get_or(params, "max_force")) {}
  tk::Values step(const tk::Context&, const tk::Values& in) override { return {{"x", std::min(tk::get_or(in, "force"), max)}}; }
  double max;
};
struct Bird final : tk::Block {
  tk::Values step(const tk::Context&, const tk::Values&) override { return {{"y", 1.0}}; }
};

const tk::Registry& test_registry() {
  static const tk::Registry registry = [] {
    tk::Registry r = tk::builtins();
    r.add(tk::define_block<Motor>("test", {.type = "motor", .label = "Motor", .kind = tk::Kind::Source,
                                           .inputs = {{"force"}}, .outputs = {{"x"}},
                                           .params = {{.id = "max_force", .default_value = 50}}}));
    r.add(tk::define_block<Bird>("test", {.type = "bird", .label = "Bird", .kind = tk::Kind::Probe, .outputs = {{"y"}}}));
    return r;
  }();
  return registry;
}

}  // namespace

TEST_CASE("the fixture descriptors load, and their toolkit blocks can be made") {
  int seen = 0;
  for (const auto& file : std::filesystem::directory_iterator(PAPERBOAT_TEST_DATA)) {
    if (file.path().extension() != ".toml") continue;
    ++seen;
    INFO(file.path().string());
    const auto catalog = tk::load_descriptor_file(file.path(), rig::registry());
    CHECK(catalog.domain == file.path().stem().string());
    CHECK_FALSE(catalog.entries.empty());
    CHECK(nlohmann::json::parse(catalog.graph).contains("nodes"));
    for (const auto& e : catalog.entries) {
      if (!e.from) continue;
      if (tk::has_step_ports(e.def)) CHECK(e.make_step() != nullptr);
      else CHECK(e.make() != nullptr);
    }
  }
  CHECK(seen >= 1);
}

TEST_CASE("starters: optional, in protocol shape, checked against the catalog") {
  const auto none = tk::load_descriptor(descriptor("import = [\"base\"]\n"));
  CHECK(nlohmann::json::parse(none.graph) == nlohmann::json{{"nodes", nlohmann::json::array()}, {"edges", nlohmann::json::array()}});
  CHECK(none.station.empty());

  const auto some = tk::load_descriptor(descriptor(R"(
import = ["signal", "math", "rig"]
[graph]
nodes = [{ id = "g", type = "gain", params = { k = 2 }, pos = [0, 0], ports = { top = ["u"] } }, { id = "s", type = "sine" }]
edges = [{ from = ["s", "y"], to = ["g", "u"] }]
[station]
objects = [{ id = "cp", type = "slider", props = { x0 = 0.1 } }]
)"), rig::registry());
  const auto graph = nlohmann::json::parse(some.graph);
  CHECK(graph["nodes"][1]["params"] == nlohmann::json::object());  // filled in
  CHECK(graph["edges"][0]["to"] == nlohmann::json{"g", "u"});
  CHECK(graph["nodes"][0]["ports"]["top"] == nlohmann::json{"u"});  // the UI's, kept as given
  CHECK(nlohmann::json::parse(some.station)["objects"][0]["id"] == "cp");

  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"base\"]\n[graph]\nnodes = [{ id = \"x\", type = \"gian\" }]\n")),
                    ContainsSubstring("graph.nodes[0]: no block type \"gian\""));
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"base\"]\n[graph]\nedges = [{ from = [\"a\", \"y\"], to = [\"b\", \"u\"] }]\n")),
                    ContainsSubstring("no node \"a\""));
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"base\"]\n[graph]\nnodes = [{ id = \"x\", type = \"clock\", ports = { up = [\"t\"] } }]\n")),
                    ContainsSubstring("graph.nodes[0].ports: unknown field \"up\""));
}

TEST_CASE("starter groups: subsystems nest, their parents checked") {
  const auto nested = tk::load_descriptor(descriptor(R"(
import = ["signal", "math"]
[graph]
nodes = [{ id = "s", type = "sine" }, { id = "g", type = "gain" }]
edges = [{ from = ["s", "y"], to = ["g", "u"] }]
groups = [{ id = "outer", kind = "subsystem", nodes = ["s"], ports = { left = ["g::y"] } },
          { id = "inner", kind = "subsystem", nodes = ["g"], parent = "outer" }]
)"));
  const auto graph = nlohmann::json::parse(nested.graph);
  CHECK(graph["groups"][1]["parent"] == "outer");
  CHECK(graph["groups"][0]["ports"]["left"][0] == "g::y");

  const auto bad = [](const std::string& groups) {
    return descriptor("import = [\"signal\"]\n[graph]\nnodes = [{ id = \"s\", type = \"sine\" }]\ngroups = " + groups + "\n");
  };
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], parent = "nope" }])")),
                    ContainsSubstring("no group \"nope\" for its parent"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], parent = "b" }, { id = "b", kind = "subsystem", nodes = [], parent = "a" }])")),
                    ContainsSubstring("is inside itself"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "box", nodes = ["s"] }])")),
                    ContainsSubstring("kind must be subsystem, story or visual"));
  // A story's released outputs: [node, port] of a block in the story; only stories release.
  CHECK_NOTHROW(tk::load_descriptor(bad(R"([{ id = "a", kind = "story", nodes = ["s"], release = [["s", "y"]] }])")));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], release = [["s", "y"]] }])")),
                    ContainsSubstring("only a story releases outputs"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "story", nodes = [], release = [["s", "y"]] }])")),
                    ContainsSubstring("which isn't in the story"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "story", nodes = ["s"], release = ["s.y"] }])")),
                    ContainsSubstring("release lists outputs as [node, port]"));
  // Exposed ports: { side, to = [node, port] } on a subsystem or story, wired or not.
  CHECK_NOTHROW(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], expose = [{ side = "out", to = ["s", "y"] }] }])")));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "visual", nodes = ["s"], expose = [{ side = "out", to = ["s", "y"] }] }])")),
                    ContainsSubstring("only a subsystem or story exposes ports"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], expose = [{ side = "up", to = ["s", "y"] }] }])")),
                    ContainsSubstring("side must be in or out"));
  CHECK_THROWS_WITH(tk::load_descriptor(bad(R"([{ id = "a", kind = "subsystem", nodes = ["s"], expose = [{ side = "in", to = ["nope", "u"] }] }])")),
                    ContainsSubstring("no node \"nope\""));
}

TEST_CASE("import adds every template of a package; a customised entry wins") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
import = ["math", "signal", "step"]

[[blocks]]
use = "math/gain"
defaults = { k = -1 }
)"), test_registry());
  // The domain's own package ("test": motor, bird) comes without being imported.
  // (signal's abstract parent, generator, isn't imported: it is only a parent)
  CHECK(catalog.entries.size() == 2 + tk::builtins().package("math").size() + tk::builtins().package("signal").size() - 1 +
                                     tk::builtins().package("step").size());
  CHECK(catalog.find("generator") == nullptr);
  CHECK(catalog.find("gain")->def.params.at(0).default_value == -1.0);  // not replaced by the import
  CHECK(catalog.find("sine") != nullptr);
  CHECK(catalog.find("when") != nullptr);
  CHECK(catalog.find("pid") == nullptr);  // control wasn't imported
  CHECK(catalog.find("key") == nullptr);  // nor base
  CHECK(catalog.find("motor")->group == "test");
  CHECK(catalog.find("gain")->group == "math");
  CHECK(catalog.find("when")->group == "step");
  CHECK(tk::catalog_message(catalog).find(R"("group":"step")") != std::string::npos);

  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"bse\"]\n")),
                    Catch::Matchers::ContainsSubstring("no package \"bse\""));
}

TEST_CASE("a key param is text: its default and value reach the block") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "base/key"
type = "key-down"
defaults = { key = "ArrowDown" }
)"));
  const auto* e = catalog.find("key-down");
  CHECK(e->def.params.at(0).default_text == "ArrowDown");
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["blocks"][0]["params"][0] == nlohmann::json{{"id", "key"}, {"type", "key"}, {"default", "ArrowDown"}});

  tk::KeySet keys{"ArrowDown"};
  const tk::Context ctx{.t = 0, .dt = 0.01, .keys_down = &keys};
  CHECK(tk::get_or(e->make()->step(ctx, {}), "pressed") == 1.0);                     // the default
  CHECK(tk::get_or(e->make({}, {{"key", "KeyA"}})->step(ctx, {}), "pressed") == 0.0);  // set in the UI
}

TEST_CASE("a template entry can be renamed, relabelled and given new defaults") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "control/pid"
type = "pid-theta"
label = "PID θ"   # comments are fine
defaults = { kp = 40, kd = 8 }
)"));
  const auto* e = catalog.find("pid-theta");
  REQUIRE(e);
  CHECK(e->def.label == "PID θ");
  CHECK(e->from == tk::builtins().find("control/pid"));
  CHECK(e->def.params[0].id == "kp");
  CHECK(e->def.params[0].default_value == 40);
  // The template itself is untouched.
  CHECK(tk::builtins().find("control/pid")->def.params[0].default_value == 1);
}

TEST_CASE("the same template can appear twice under different types") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "base/key"
type = "key-left"
defaults = { key = "ArrowLeft" }

[[blocks]]
use = "base/key"
type = "key-right"
defaults = { key = "ArrowRight" }
)"));
  REQUIRE(catalog.entries.size() == 2);
  CHECK(catalog.find("key-left")->def.params.at(0).default_text == "ArrowLeft");
  CHECK(catalog.find("key-right")->def.params.at(0).default_text == "ArrowRight");

  tk::KeySet keys{"ArrowRight"};
  tk::Context ctx{.t = 0, .dt = 0.01, .keys_down = &keys};
  CHECK(catalog.find("key-left")->make()->step(ctx, {}).at("pressed") == 0.0);
  CHECK(catalog.find("key-right")->make()->step(ctx, {}).at("pressed") == 1.0);
}

TEST_CASE("make() fills params the caller leaves out from the entry's defaults") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "math/gain"
defaults = { k = -2 }
)"));
  const auto* gain = catalog.find("gain");
  tk::Context ctx{.t = 0, .dt = 0.01};
  CHECK(gain->make()->step(ctx, {{"u", 3}}).at("y") == -6);
  CHECK(gain->make({{"k", 5}})->step(ctx, {{"u", 3}}).at("y") == 15);
}

TEST_CASE("a domain defines its blocks in code; its descriptor gets them without listing them") {
  const auto catalog = tk::load_descriptor("domain = \"test\"\n", test_registry());
  const auto* motor = catalog.find("motor");
  REQUIRE(motor);
  CHECK(motor->group == "test");
  CHECK(motor->from == test_registry().find("test/motor"));
  CHECK(tk::get_or(motor->make()->step({}, {{"force", 80}}), "x") == 50);  // its default max_force

  // Customised like any template: a new type, label and default.
  const auto custom = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "test/motor"
type = "winch"
defaults = { max_force = 10 }
)"), test_registry());
  CHECK(tk::get_or(custom.find("winch")->make()->step({}, {{"force", 80}}), "x") == 10);
  CHECK(custom.find("motor") != nullptr);  // renamed, so the default type stays too (as with imports)
  CHECK(custom.find("bird") != nullptr);

  // A descriptor never defines a block itself.
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("[[blocks]]\ntype = \"motor\"\nkind = \"source\"\n")),
                    ContainsSubstring("needs \"use\": a domain defines its own blocks in code"));
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"empty\"\n"), ContainsSubstring("the catalog is empty"));
}

TEST_CASE("[station] says how the Station page shows the station") {
  CHECK(tk::load_descriptor("domain = \"test\"\n", test_registry()).station_view == "scene");
  const auto graph = tk::load_descriptor("domain = \"test\"\n[station]\nview = \"graph\"\n", test_registry());
  CHECK(graph.station_view == "graph");
  CHECK(nlohmann::json::parse(graph.station)["objects"].empty());
  const auto web = tk::load_descriptor("domain = \"test\"\n[station]\nview = \"web\"\npage = \"station/index.html\"\n",
                                       test_registry());
  CHECK(web.station_page == "station/index.html");
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[station]\nview = \"web\"\n", test_registry()),
                    ContainsSubstring("needs a page"));
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[station]\nview = \"web\"\npage = \"../x.html\"\n",
                                        test_registry()),
                    ContainsSubstring("inside the backend's folder"));
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[station]\nview = \"vr\"\n", test_registry()),
                    ContainsSubstring("scene, graph or web"));
}

TEST_CASE("[dashboard] is the default layout of the Dashboard's grid") {
  CHECK(tk::load_descriptor("domain = \"test\"\n", test_registry()).dashboard.empty());
  const auto catalog = tk::load_descriptor(R"(domain = "test"
[dashboard]
columns = 6
instruments = [
  { block = "graph-theta", size = "large", at = [0, 0] },
  { block = "speed" },
  { block = "camera", size = "xlarge" },
]
)", test_registry());
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["dashboard"]["columns"] == 6);
  CHECK(msg["dashboard"]["instruments"][0] == nlohmann::json{{"block", "graph-theta"}, {"size", "large"}, {"at", {0, 0}}});
  CHECK(msg["dashboard"]["instruments"][2]["size"] == "xlarge");
  CHECK(msg["dashboard"]["instruments"][1] == nlohmann::json{{"block", "speed"}});
  CHECK_FALSE(nlohmann::json::parse(tk::catalog_message(tk::load_descriptor("domain = \"test\"\n", test_registry())))
                  .contains("dashboard"));

  auto fails_with = [](const std::string& dashboard, const std::string& text) {
    INFO(dashboard);
    CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[dashboard]\n" + dashboard, test_registry()),
                      ContainsSubstring(text));
  };
  fails_with("columns = 0", "from 1 to 12");
  fails_with("rows = 3", "unknown field \"rows\"");
  fails_with("instruments = [{ size = \"large\" }]", "needs \"block\"");
  fails_with("instruments = [{ block = \"a\", size = \"huge\" }]", "compact, normal, large or xlarge");
  fails_with("instruments = [{ block = \"a\", at = [1] }]", "[column, row]");
  fails_with("instruments = [{ block = \"a\", at = [-1, 0] }]", "[column, row]");
  fails_with("instruments = [{ block = \"a\" }, { block = \"a\" }]", "placed twice");
}

TEST_CASE("[dashboard] decorations and tabs: sections, separators, labels, pages") {
  const auto catalog = tk::load_descriptor(R"(domain = "test"
[dashboard]
columns = 6
tabs = ["Run", "Tuning"]
instruments = [{ block = "theta", size = "large", at = [0, 1], tab = "Run" }, { block = "gains", tab = "Tuning" }]
decorations = [
  { kind = "label", text = "Pendulum", at = [0, 0], span = [3, 1], style = "heading" },
  { kind = "section", text = "Controller", at = [3, 0], span = [3, 2] },
  { kind = "hline", at = [0, 3], span = 6 },
  { kind = "vline", at = [3, 0], span = 2, tab = "Tuning" },
  { kind = "label", text = "Notes", at = [0, 4] },
  { kind = "console", at = [0, 5], span = [6, 3] },
  { kind = "controls", at = [0, 8], span = [4, 3] },
]
)", test_registry());
  const auto d = nlohmann::json::parse(tk::catalog_message(catalog))["dashboard"];
  CHECK(d["tabs"] == nlohmann::json::array({"Run", "Tuning"}));
  CHECK(d["instruments"][0]["tab"] == "Run");
  CHECK(d["decorations"].size() == 7);
  CHECK(d["decorations"][6] == nlohmann::json{{"kind", "controls"}, {"at", {0, 8}}, {"span", {4, 3}}});
  CHECK(d["decorations"][5] == nlohmann::json{{"kind", "console"}, {"at", {0, 5}}, {"span", {6, 3}}});
  CHECK(d["decorations"][0] == nlohmann::json{{"kind", "label"}, {"at", {0, 0}}, {"span", {3, 1}}, {"text", "Pendulum"}, {"style", "heading"}});
  CHECK(d["decorations"][2] == nlohmann::json{{"kind", "hline"}, {"at", {0, 3}}, {"span", 6}});
  CHECK(d["decorations"][3]["tab"] == "Tuning");
  // A layout without them is as before: no tabs, no decorations.
  const auto plain = nlohmann::json::parse(tk::catalog_message(tk::load_descriptor("domain = \"test\"\n[dashboard]\ncolumns = 4\n", test_registry())))["dashboard"];
  CHECK_FALSE(plain.contains("tabs"));
  CHECK_FALSE(plain.contains("decorations"));

  auto fails_with = [](const std::string& dashboard, const std::string& text) {
    INFO(dashboard);
    CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[dashboard]\ncolumns = 6\ntabs = [\"Run\"]\n" + dashboard, test_registry()),
                      ContainsSubstring(text));
  };
  fails_with("decorations = [{ kind = \"frame\", at = [0, 0] }]", "kind must be section, hline, vline, label, console, station or controls");
  fails_with("decorations = [{ kind = \"label\", text = \"x\" }]", "needs \"at\"");
  fails_with("decorations = [{ kind = \"label\", at = [0, 0] }]", "a label needs its text");
  fails_with("decorations = [{ kind = \"label\", text = \"x\", at = [6, 0] }]", "past the last of 6");
  fails_with("decorations = [{ kind = \"section\", at = [4, 0], span = [3, 2] }]", "spans past the last column");
  fails_with("decorations = [{ kind = \"hline\", at = [0, 0], span = 7 }]", "spans past the last column");
  fails_with("decorations = [{ kind = \"hline\", at = [0, 0] }]", "needs a span");
  fails_with("decorations = [{ kind = \"hline\", at = [0, 0], span = [2, 1] }]", "whole number of cells");
  fails_with("decorations = [{ kind = \"section\", at = [0, 0], span = 3 }]", "[columns, rows]");
  fails_with("decorations = [{ kind = \"hline\", at = [0, 0], span = 2, text = \"x\" }]", "has no text");
  fails_with("decorations = [{ kind = \"section\", at = [0, 0], span = [2, 2], style = \"note\" }]", "only a label has a style");
  fails_with("decorations = [{ kind = \"console\", at = [0, 0], text = \"x\" }]", "the console has no text");
  fails_with("decorations = [{ kind = \"controls\", at = [0, 0], text = \"x\" }]", "the controls tile has no text");
  fails_with("decorations = [{ kind = \"console\", at = [0, 0], span = 3 }]", "[columns, rows]");
  fails_with("decorations = [{ kind = \"station\", at = [0, 0], text = \"x\" }]", "the station has no text");
  fails_with("decorations = [{ kind = \"station\", at = [0, 0], span = 3 }]", "[columns, rows]");
  fails_with("decorations = [{ kind = \"label\", text = \"x\", at = [0, 0], style = \"big\" }]", "heading or note");
  fails_with("decorations = [{ kind = \"label\", text = \"x\", at = [0, 0], tab = \"Other\" }]", "tab must be one of dashboard.tabs");
  fails_with("instruments = [{ block = \"a\", tab = \"Other\" }]", "tab must be one of dashboard.tabs");
  fails_with("decorations = [{ kind = \"label\", text = \"x\", at = [0, 0], colour = \"red\" }]", "unknown field \"colour\"");
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[dashboard]\ntabs = [\"a\", \"a\"]\n", test_registry()), ContainsSubstring("is twice"));
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"test\"\n[dashboard]\ninstruments = [{ block = \"a\", tab = \"x\" }]\n", test_registry()),
                    ContainsSubstring("(there are none)"));
}

TEST_CASE("a block's view: built-in for simple maths, inherited, overridden one key at a time") {
  const auto msg = [](const std::string& blocks) {
    return nlohmann::json::parse(tk::catalog_message(tk::load_descriptor(descriptor(blocks), test_registry())));
  };
  auto block = [](const nlohmann::json& m, const std::string& type) {
    for (const auto& b : m["blocks"]) if (b["type"] == type) return b;
    FAIL("no block " << type);
    return nlohmann::json{};
  };
  // Simple maths blocks are compact plates by default…
  const auto base = msg("[[blocks]]\nuse = \"math/gain\"\n[[blocks]]\nuse = \"math/sum\"\n[[blocks]]\nuse = \"control/pid\"\n");
  CHECK(block(base, "gain")["view"] == nlohmann::json{{"compact", true}, {"glyph", "gain"}, {"value", "{k}"}});
  CHECK(block(base, "sum")["view"] == nlohmann::json{{"compact", true}, {"glyph", "plus"}});
  // …and a block with no view is the normal card: the key is absent.
  CHECK_FALSE(block(base, "pid").contains("view"));

  // A definition inherits its parent's view and overrides single keys of it.
  const auto child = msg("[[blocks]]\nuse = \"math/gain\"\ntype = \"half\"\ndefaults = { k = 0.5 }\nview = { glyph = \"plus\" }\n");
  CHECK(block(child, "half")["view"] == nlohmann::json{{"compact", true}, {"glyph", "plus"}, {"value", "{k}"}});
  // compact = false turns a compact parent back into a card; the other keys stay.
  const auto card = msg("[[blocks]]\nuse = \"math/gain\"\nview = { compact = false }\n");
  CHECK(block(card, "gain")["view"] == nlohmann::json{{"compact", false}, {"glyph", "gain"}, {"value", "{k}"}});

  auto fails_with = [](const std::string& view, const std::string& text) {
    INFO(view);
    CHECK_THROWS_WITH(tk::load_descriptor(descriptor("[[blocks]]\nuse = \"math/gain\"\nview = " + view + "\n"), test_registry()),
                      ContainsSubstring(text));
  };
  fails_with("{ symbol = \"x\" }", "unknown field \"symbol\"");
  fails_with("{ compact = \"yes\" }", "compact must be true or false");
  fails_with("{ glyph = 3 }", "glyph must be text");
}

TEST_CASE("catalog_message matches the protocol's catalog shape") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
[[blocks]]
use = "math/threshold"
defaults = { level = 0.3 }
)"), test_registry());
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["type"] == "catalog");
  // The domain's own blocks come first, then each toolkit package.
  CHECK(msg["blocks"][0]["type"] == "motor");
  CHECK(msg["blocks"][1]["kind"] == "probe");
  CHECK(msg["blocks"][1]["group"] == "test");
  CHECK(msg["blocks"][1]["params"] == nlohmann::json::array());
  const auto& threshold = msg["blocks"][2];
  CHECK(threshold["type"] == "threshold");
  CHECK(threshold["group"] == "math");
  CHECK(threshold["kind"] == "function");
  CHECK(threshold["inputs"][0] == nlohmann::json{{"id", "u"}, {"type", "signal"}});
  CHECK(threshold["params"][0] == nlohmann::json{{"id", "level"}, {"type", "number"}, {"default", 0.3}});
}

TEST_CASE("descriptor mistakes are reported with where they are") {
  auto fails_with = [](const std::string& blocks, const std::string& text) {
    INFO(blocks);
    CHECK_THROWS_WITH(tk::load_descriptor(descriptor(blocks)), ContainsSubstring(text));
  };
  fails_with("import = [\"opencv\"]", R"(import: no package "opencv" (known: )");
  fails_with("[[blocks]]\nuse = \"control/pdi\"", R"(blocks[0] (control/pdi): no template "control/pdi" (known: )");
  fails_with("[[blocks]]\nuse = \"control/pid\"\ndefaults = { kq = 1 }", R"(control/pid has no param "kq")");
  fails_with("[[blocks]]\nuse = \"control/pid\"\nconfig = { key = \"x\" }", R"(control/pid has no config "key")");
  fails_with("[[blocks]]\nuse = \"math/sum\"\n[[blocks]]\nuse = \"math/sum\"",
             R"(blocks[1] (math/sum): duplicate type "sum")");
  fails_with("[[blocks]]\nuse = \"math/sum\"\nlable = \"Add\"", R"(unknown field "lable")");
  fails_with("[[blocks]]\ntype = \"motor\"", R"(blocks[0] (motor): needs "use")");
  fails_with("[[blocks]]\nuse = \"math/sum\"\nadded = 2026-11-02", "line 4: dates and times");
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"x\"\n[[blocks]\n"), ContainsSubstring("not valid TOML: line 2"));
  CHECK_THROWS_AS(tk::load_descriptor_file("/no/such/descriptor.toml"), tk::DescriptorError);
}

TEST_CASE("an enum param: one of its options, in the catalog with them") {
  const auto catalog = tk::load_descriptor(descriptor(R"toml(
[[blocks]]
use = "signal/generator"
type = "pick"
params = { which = { default = "b", type = "enum", options = ["a", "b", "c"] } }
config = { f = "which * 10" }
)toml"));
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["blocks"][0]["params"][0] ==
        nlohmann::json{{"id", "which"}, {"type", "enum"}, {"default", "b"}, {"options", {"a", "b", "c"}}});
  CHECK(tk::get_or(catalog.find("pick")->make()->step({}, {{"t", 0.0}}), "y") == 10.0);  // b is index 1
  CHECK(tk::get_or(catalog.find("pick")->make({}, {{"which", "c"}})->step({}, {{"t", 0.0}}), "y") == 20.0);
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("[[blocks]]\nuse = \"signal/ease\"\ndefaults = { shape = \"wobbly\" }\n")),
                    ContainsSubstring("must be one of its options"));
}

TEST_CASE("a boolean param is a number to the block and true or false in the catalog") {
  const auto catalog = tk::load_descriptor(descriptor("[[blocks]]\nuse = \"base/boolean\"\ndefaults = { value = false }\n"));
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["blocks"][0]["params"][0] == nlohmann::json{{"id", "value"}, {"type", "boolean"}, {"default", false}});
  CHECK(tk::get_or(catalog.find("boolean")->make()->step({}, {}), "y") == 0.0);
}

TEST_CASE("a descriptor defines a block on a C++ parent: new params and its expression") {
  const auto catalog = tk::load_descriptor(descriptor(R"toml(
[[blocks]]
use = "signal/generator"
type = "wobble"
label = "Wobble"
params = { size = 2, fast = true }
config = { f = "size * sin((fast ? 4 : 1) * t)" }
)toml"));
  const auto* wobble = catalog.find("wobble");
  REQUIRE(wobble != nullptr);
  CHECK(wobble->group == "test");  // a definition of the domain's own
  CHECK(wobble->def.params.at(1).type == "boolean");
  CHECK(tk::get_or(wobble->make()->step({.t = 0}, {{"t", 0.0}}), "y") == 0.0);
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("[[blocks]]\nuse = \"signal/generator\"\ntype = \"bad\"\nconfig = { f = \"sin(q)\" }\n")),
                    ContainsSubstring("f: at 5: unknown name \"q\""));
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("[[blocks]]\nuse = \"math/gain\"\nparams = { j = 1 }\n")),
                    ContainsSubstring("takes no new params"));
}

TEST_CASE("blocks and objects carry a one-line description to the catalog") {
  const auto catalog = tk::load_descriptor(descriptor(R"(
import = ["control", "signal"]
[[blocks]]
use = "math/gain"
type = "double"
description = "Twice its input."
defaults = { k = 2 }
)"));
  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  auto find = [&](const std::string& type) {
    for (const auto& b : msg["blocks"])
      if (b["type"] == type) return b;
    return nlohmann::json();
  };
  CHECK(find("pid")["description"].get<std::string>().find("error") != std::string::npos);  // built in
  CHECK(find("sine")["description"] == "A smooth wave: amplitude, frequency, phase.");       // from TOML
  CHECK(find("double")["description"] == "Twice its input.");                              // the descriptor's own
}

TEST_CASE("built-in templates carry their own descriptions") {
  std::vector<std::string> missing;
  for (const auto* entry : tk::builtins().all())
    if (!entry->abstract && entry->def.description.empty()) missing.push_back(entry->name);
  CHECK(missing.empty());
}

TEST_CASE("toolkits manifest, requirements detection, and installer lifecycle") {
  namespace fs = std::filesystem;
  const auto parsed = tk::parse_toolkits_manifest(R"(
[[toolkits]]
name = "opencv"
repo = "https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv.git"
description = "Image blocks."
optional = true
engine = ">=0.1"
)");
  REQUIRE(parsed.size() == 1);
  CHECK(parsed[0].name == "opencv");
  CHECK(parsed[0].optional);
  CHECK(parsed[0].engine == ">=0.1");

  const fs::path tmp = fs::temp_directory_path() / "paperboat_toolkit_installer_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  const fs::path root = tmp / "repo";
  const fs::path backend_dir = tmp / "backend";
  const fs::path remote = tmp / "remote-opencv";
  fs::create_directories(root / "engine" / "toolkit");
  fs::create_directories(backend_dir);
  fs::create_directories(remote);

  {
    std::ofstream out(root / "toolkits.toml");
    out << "[[toolkits]]\nname = \"opencv\"\nrepo = \"" << remote.string()
        << "\"\ndescription = \"OpenCV toolkit\"\noptional = true\nengine = \">=0.1\"\n";
  }
  {
    std::ofstream out(backend_dir / "toolkit_requirements.toml");
    out << "toolkits = [\"opencv\"]\n";
  }
  {
    std::ofstream out(remote / "toolkit.toml");
    out << "name = \"opencv\"\nregister = [\"register_opencv\"]\n";
  }
  REQUIRE(std::system(("git -C '" + remote.string() +
                       "' init -q && git -C '" + remote.string() +
                       "' config user.email test@example.com && git -C '" + remote.string() +
                       "' config user.name test && git -C '" + remote.string() +
                       "' add toolkit.toml && git -C '" + remote.string() + "' commit -qm init")
                          .c_str()) == 0);

  auto list = tk::list_toolkits(root, backend_dir);
  REQUIRE(list.size() == 2);
  CHECK(list[0].name == "builtin");
  CHECK(list[0].builtin);
  CHECK(list[0].installed);
  CHECK(list[1].name == "opencv");
  CHECK_FALSE(list[1].installed);
  CHECK(list[1].required);

  std::vector<std::string> logs;
  auto logger = [&](std::string_view, std::string_view text) { logs.emplace_back(text); };

  const auto installed = tk::install_toolkit("opencv", "dev", root, {.source_dir = {}, .build_dir = {}, .skip_rebuild = true}, logger);
  CHECK(installed.ok);
  CHECK(fs::is_regular_file(root / "engine" / "toolkit" / "opencv" / "toolkit.toml"));
  CHECK(tk::list_toolkits(root, backend_dir)[1].installed);

  const auto removed = tk::uninstall_toolkit("opencv", root, {.source_dir = {}, .build_dir = {}, .skip_rebuild = true}, logger);
  CHECK(removed.ok);
  CHECK_FALSE(fs::exists(root / "engine" / "toolkit" / "opencv"));
  CHECK_FALSE(tk::list_toolkits(root, backend_dir)[1].installed);
  CHECK_FALSE(logs.empty());

  fs::remove_all(tmp, ec);
}

TEST_CASE("parse_release_tags sorts semver tags newest first") {
  const std::string mock_tags = R"(
f97ce82222540c918b12ecbdc9eb7b8bf25c1bcc	refs/tags/v0.1.0
1234567890abcdef1234567890abcdef12345678	refs/tags/0.3.0
abcdef1234567890abcdef1234567890abcdef12	refs/tags/v0.10.0
deadbeefdeadbeefdeadbeefdeadbeefdeadbeef	refs/tags/v0.2.0
beefdeadbeefdeadbeefdeadbeefdeadbeefdead	refs/tags/nightly
cafebabecafebabecafebabecafebabecafebabe	refs/tags/v1.0.0-rc1
)";
  const auto tags = tk::parse_release_tags(mock_tags);
  REQUIRE(tags.size() == 4);
  CHECK(tags[0] == "0.10.0");
  CHECK(tags[1] == "0.3.0");
  CHECK(tags[2] == "0.2.0");
  CHECK(tags[3] == "0.1.0");
}

TEST_CASE("optional imports in descriptor ignore missing packages and clean starters") {
  const std::string toml = R"(
domain = "test"
import = ["optional_pkg", "signal"]

[graph]
nodes = [
  { id = "sine-1", type = "sine", pos = [0, 0] },
  { id = "missing-1", type = "non_existent_block", pos = [100, 0] }
]
edges = [
  { from = ["sine-1", "y"], to = ["missing-1", "in"] }
]

[station]
objects = [
  { id = "obj-1", type = "non_existent_object" }
]
)";

  // Without optional_imports, it throws
  CHECK_THROWS_AS(tk::load_descriptor(toml, test_registry()), tk::DescriptorError);

  // With optional_imports, it succeeds and keeps nodes and objects even if types are not in catalog
  const auto cat = tk::load_descriptor(toml, test_registry(), {.optional_imports = {"optional_pkg"}});
  CHECK(cat.missing_imports.size() == 1);
  CHECK(cat.missing_imports[0] == "optional_pkg");

  const auto graph = nlohmann::json::parse(cat.graph);
  REQUIRE(graph["nodes"].size() == 2);
  CHECK(graph["nodes"][0]["id"] == "sine-1");
  CHECK(graph["nodes"][1]["id"] == "missing-1");
  CHECK(graph["edges"].size() == 1);

  const auto station = nlohmann::json::parse(cat.station);
  REQUIRE(station["objects"].size() == 1);
  CHECK(station["objects"][0]["id"] == "obj-1");
}
