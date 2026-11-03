#include <algorithm>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/toolkit/descriptor.hpp"

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
    const auto catalog = tk::load_descriptor_file(file.path());
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
import = ["signal", "math", "control"]
[graph]
nodes = [{ id = "g", type = "gain", params = { k = 2 }, pos = [0, 0] }, { id = "s", type = "sine" }]
edges = [{ from = ["s", "y"], to = ["g", "u"] }]
[station]
objects = [{ id = "cp", type = "cart-pole", props = { theta0 = 0.1 } }]
)"));
  const auto graph = nlohmann::json::parse(some.graph);
  CHECK(graph["nodes"][1]["params"] == nlohmann::json::object());  // filled in
  CHECK(graph["edges"][0]["to"] == nlohmann::json{"g", "u"});
  CHECK(nlohmann::json::parse(some.station)["objects"][0]["id"] == "cp");

  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"base\"]\n[graph]\nnodes = [{ id = \"x\", type = \"gian\" }]\n")),
                    ContainsSubstring("graph.nodes[0]: no block type \"gian\""));
  CHECK_THROWS_WITH(tk::load_descriptor(descriptor("import = [\"base\"]\n[graph]\nedges = [{ from = [\"a\", \"y\"], to = [\"b\", \"u\"] }]\n")),
                    ContainsSubstring("no node \"a\""));
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
