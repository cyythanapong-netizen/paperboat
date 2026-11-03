#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "paperboat/core/station.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace tk = paperboat::toolkit;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("import brings a package's station objects and their linked blocks") {
  const auto catalog = tk::load_descriptor("domain = \"d\"\nimport = [\"control\"]\n");
  const auto* object = catalog.find_object("cart-pole");
  REQUIRE(object != nullptr);
  CHECK(object->group == "control");
  const auto* linked = catalog.find("cart-pole");
  REQUIRE(linked != nullptr);
  CHECK(linked->linked);
  CHECK(linked->def.params.empty());  // its properties live on the station object

  const auto msg = nlohmann::json::parse(tk::catalog_message(catalog));
  CHECK(msg["objects"][0]["type"] == "cart-pole");
  CHECK(msg["objects"][0]["block"] == "cart-pole");
  bool flagged = false;
  for (const auto& b : msg["blocks"]) flagged |= b["type"] == "cart-pole" && b.value("linked", false);
  CHECK(flagged);
}

TEST_CASE("a cart-pole object starts at its initial conditions, falls, and is pushed") {
  const auto catalog = tk::load_descriptor("domain = \"d\"\nimport = [\"control\"]\n");
  const auto* entry = catalog.find_object("cart-pole");
  auto still = entry->make({{"theta0", 0.0}});
  auto tilted = entry->make({{"theta0", 0.1}, {"x0", 1.0}});
  CHECK(tilted->read().at("x") == 1.0);
  CHECK(tilted->read().at("theta") == 0.1);

  const tk::Context ctx{.t = 0, .dt = 0.01, .keys_down = nullptr};
  for (int i = 0; i < 50; ++i) {
    still->step(ctx, {});
    tilted->step(ctx, {});
  }
  CHECK(still->read().at("theta") == 0.0);   // balanced exactly: stays
  CHECK(tk::get_or(tilted->read(), "theta") > 0.1);   // leaning: falls further

  auto pushed = entry->make({{"theta0", 0.0}});
  for (int i = 0; i < 50; ++i) pushed->step(ctx, {{"force", 1000.0}});  // clamped to max_force
  CHECK(tk::get_or(pushed->read(), "v") > 0);
  CHECK(tk::get_or(pushed->read(), "v") < 30.0);  // 50 N, not 1000 N: unclamped it would pass 400 m/s

  const auto shapes = tilted->entities("cp-1", -1.0);
  REQUIRE(shapes.size() == 4);
  CHECK(shapes[0].id == "cp-1/rail");
  CHECK(shapes[1].pos[2] == -1.0);
}

namespace {

// A domain's own station object, defined in code: a trolley pushed along a rail.
struct Trolley final : tk::StationObject {
  explicit Trolley(const tk::Values& props) : mass(tk::get_or(props, "mass")), x(tk::get_or(props, "x0")) {}
  tk::Values read() const override { return {{"x", x}}; }
  void step(const tk::Context& ctx, const tk::Values& in) override { x += tk::get_or(in, "force") / mass * ctx.dt; }
  std::vector<tk::Entity> entities(const std::string& id, double z) const override {
    return {{.id = id + "/body", .shape = "box", .pos = {x, 0, z}}};
  }
  double mass, x;
};

}  // namespace

TEST_CASE("a domain defines its own station object and linked block in code") {
  tk::Registry registry = tk::builtins();
  registry.add(tk::define_object<Trolley>(
      "crane", {.type = "trolley", .label = "Trolley", .role = "body",
                .props = {{.id = "mass", .default_value = 200}, {.id = "x0", .default_value = 0}}, .links = {},
                .block = tk::BlockDef{.type = "trolley", .label = "Trolley", .kind = tk::Kind::Source,
                                      .inputs = {{"force"}}, .outputs = {{"x"}}}}));
  const auto catalog = tk::load_descriptor("domain = \"crane\"\n", registry);
  const auto* object = catalog.find_object("trolley");
  REQUIRE(object != nullptr);
  CHECK(object->group == "crane");
  CHECK(catalog.find("trolley")->linked);
  auto trolley = object->make({{"x0", 1}});
  trolley->step({.t = 0, .dt = 1}, {{"force", 400}});
  CHECK(tk::get_or(trolley->read(), "x") == 3.0);  // 1 + 400 / 200

  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"crane\"\n[[objects]]\ntype = \"trolley\"\n", registry),
                    ContainsSubstring("needs \"use\""));
}

namespace {

// Two objects that link: a rope pulls its `load` toward its own x.
struct Rope final : tk::StationObject {
  explicit Rope(const tk::Values& props) : x(tk::get_or(props, "x")) {}
  tk::Values read() const override { return {}; }
  void step(const tk::Context&, const tk::Values&) override {
    if (load) load->x = x;
  }
  std::vector<tk::Entity> entities(const std::string&, double) const override { return {}; }
  void link(std::string_view slot, tk::StationObject& other) override {
    if (slot == "load") load = dynamic_cast<Trolley*>(&other);
  }
  double x;
  Trolley* load = nullptr;
};

}  // namespace

TEST_CASE("station objects link to each other through their slots") {
  tk::Registry registry = tk::builtins();
  registry.add(tk::define_object<Trolley>("crane", {.type = "trolley", .label = "Trolley",
                                                    .props = {{.id = "mass", .default_value = 1}, {.id = "x0"}}}));
  registry.add(tk::define_object<Rope>("crane", {.type = "rope", .label = "Rope", .props = {{.id = "x"}}, .links = {"load"}}));
  const auto catalog = tk::load_descriptor(R"(
domain = "crane"
[station]
view = "graph"
objects = [
  { id = "t", type = "trolley" },
  { id = "r", type = "rope", props = { x = 4 }, links = { load = "t" } },
]
)", registry);
  auto station = paperboat::core::make_station(nlohmann::json::parse(catalog.station), catalog);
  station[1].instance->step({}, {});
  CHECK(tk::get_or(station[0].instance->read(), "x") == 4.0);  // the rope moved what it holds

  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"crane\"\n[station]\nobjects = [{ id = \"r\", type = \"rope\", "
                                        "links = { load = \"nobody\" } }]\n", registry),
                    ContainsSubstring("isn't on the station"));
  CHECK_THROWS_WITH(tk::load_descriptor("domain = \"crane\"\n[station]\nobjects = [{ id = \"t\", type = \"trolley\" }, "
                                        "{ id = \"r\", type = \"rope\", links = { hook = \"t\" } }]\n", registry),
                    ContainsSubstring("no link slot \"hook\""));
  CHECK_THROWS_WITH(tk::load_descriptor("[[objects]]\nuse = \"control/cartpole\"\n"),
                    ContainsSubstring("no station template \"control/cartpole\""));
}
