#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;

TEST_CASE("key follows keys_down for its configured key") {
  const Template* t = builtins().find("base/key");
  REQUIRE(t != nullptr);
  CHECK(t->def.type == "key");
  REQUIRE(t->def.params.size() == 1);
  CHECK(t->def.params[0].type == "key");  // set in the UI
  CHECK(t->def.params[0].default_text == "Space");

  auto space = t->make({}, {{"key", "Space"}});
  auto enter = t->make({}, {{"key", "Enter"}});
  KeySet keys{"Space"};
  Context ctx{0.0, 0.01, &keys};

  CHECK(get_or(space->step(ctx, {}), "pressed") == 1.0);
  CHECK(get_or(enter->step(ctx, {}), "pressed") == 0.0);

  keys.erase("Space");
  CHECK(get_or(space->step(ctx, {}), "pressed") == 0.0);

  CHECK(get_or(space->step(Context{0.0, 0.01, nullptr}, {}), "pressed") == 0.0);  // no input source
}

TEST_CASE("dashboard switch supports toggle and momentary modes") {
  const Template* t = builtins().find("dashboard/switch");
  REQUIRE(t != nullptr);
  CHECK(t->def.type == "switch");
  CHECK(t->def.kind == Kind::Monitor);

  // Toggle mode, initial = 1.0, key = "btn"
  auto toggle_sw = t->make({{"initial", 1.0}}, {{"mode", "toggle"}, {"key", "btn"}});
  KeySet keys;
  Context ctx{.t = 0.0, .dt = 0.01, .keys_down = &keys, .node = "sw1"};

  // Initially on
  CHECK(get_or(toggle_sw->step(ctx, {}), "y") == 1.0);

  // Press key down -> toggles to off (0.0)
  keys.insert("btn");
  CHECK(get_or(toggle_sw->step(ctx, {}), "y") == 0.0);

  // Release key -> remains off (0.0)
  keys.erase("btn");
  CHECK(get_or(toggle_sw->step(ctx, {}), "y") == 0.0);

  // Press key down again -> toggles back to on (1.0)
  keys.insert("btn");
  CHECK(get_or(toggle_sw->step(ctx, {}), "y") == 1.0);

  // Momentary mode, default node name as key fallback
  auto mom_sw = t->make({{"initial", 0.0}}, {{"mode", "momentary"}});
  Context ctx_mom{.t = 0.0, .dt = 0.01, .keys_down = &keys, .node = "mom_node"};

  // "mom_node" is not in keys -> 0.0
  CHECK(get_or(mom_sw->step(ctx_mom, {}), "y") == 0.0);

  // Insert "mom_node" -> 1.0
  keys.insert("mom_node");
  CHECK(get_or(mom_sw->step(ctx_mom, {}), "y") == 1.0);

  // Erase "mom_node" -> 0.0
  keys.erase("mom_node");
  CHECK(get_or(mom_sw->step(ctx_mom, {}), "y") == 0.0);
}

TEST_CASE("dashboard switch also answers its node id when a key is set") {
  const Template* t = builtins().find("dashboard/switch");
  REQUIRE(t != nullptr);
  auto sw = t->make({{"initial", 0.0}}, {{"mode", "momentary"}, {"key", "btn"}});
  KeySet keys{"sw1"};  // what the dashboard control sends
  Context ctx{.t = 0.0, .dt = 0.01, .keys_down = &keys, .node = "sw1"};
  CHECK(get_or(sw->step(ctx, {}), "y") == 0.0);  // first pass only latches
  CHECK(get_or(sw->step(ctx, {}), "y") == 1.0);
}

namespace {

// A Lamp made as a descriptor would make it: the op by name in config, numbers in params.
std::unique_ptr<Block> lamp(const std::string& op, double threshold, bool latch = false) {
  const Template* t = builtins().find("dashboard/lamp");
  REQUIRE(t != nullptr);
  return t->make({{"threshold", threshold}, {"latch", latch ? 1.0 : 0.0}}, {{"op", op}});
}

double lit(Block& b, double v) { return get_or(b.step(Context{}, {{"in", v}}), "y", -1.0); }

}  // namespace

TEST_CASE("dashboard lamp: definition") {
  const Template* t = builtins().find("dashboard/lamp");
  REQUIRE(t != nullptr);
  CHECK(t->def.type == "lamp");
  CHECK(t->def.kind == Kind::Monitor);
  CHECK(t->def.instrument == "lamp");
  REQUIRE(t->def.inputs.size() == 1);
  CHECK(t->def.inputs[0].id == "in");
  CHECK(t->def.inputs[0].type == "signal");
  REQUIRE(t->def.outputs.size() == 1);  // a Monitor with an output runs as a block, shown from `y`
  CHECK(t->def.outputs[0].id == "y");
  std::vector<std::string> ids;
  for (const auto& p : t->def.params) ids.push_back(p.id);
  CHECK(ids == std::vector<std::string>{"threshold", "op", "latch", "label", "color"});
  CHECK(t->def.params[0].default_value == 0.5);
  CHECK(t->def.params[1].default_text == ">");
  CHECK(t->def.params[1].options == std::vector<std::string>{">", ">=", "<", "<=", "==", "!="});
  CHECK(t->def.params[2].type == "boolean");
  CHECK(t->def.params[3].type == "text");
  CHECK(t->def.params[4].options == std::vector<std::string>{"signal", "danger"});
  CHECK(t->def.view.compact == true);
  CHECK(t->def.view.value == "{op} {threshold}");
  CHECK(t->def.view.icon.is_object());
}

TEST_CASE("dashboard lamp: each comparison against its threshold") {
  SECTION("> lights strictly above") {
    auto b = lamp(">", 10);
    CHECK(lit(*b, 9.99) == 0.0);
    CHECK(lit(*b, 10) == 0.0);
    CHECK(lit(*b, 10.01) == 1.0);
    CHECK(lit(*b, 3) == 0.0);  // not latched: off again
  }
  SECTION(">= lights at and above") {
    auto b = lamp(">=", 10);
    CHECK(lit(*b, 9.99) == 0.0);
    CHECK(lit(*b, 10) == 1.0);
  }
  SECTION("< and <= below") {
    auto below = lamp("<", -1);
    CHECK(lit(*below, -1) == 0.0);
    CHECK(lit(*below, -1.5) == 1.0);
    auto at_most = lamp("<=", -1);
    CHECK(lit(*at_most, -1) == 1.0);
    CHECK(lit(*at_most, 0) == 0.0);
  }
  SECTION("== and !=") {
    auto eq = lamp("==", 3);
    CHECK(lit(*eq, 3) == 1.0);
    CHECK(lit(*eq, 3.0001) == 0.0);
    auto ne = lamp("!=", 0);  // on when true: a boolean that isn't 0
    CHECK(lit(*ne, 0) == 0.0);
    CHECK(lit(*ne, 1) == 1.0);
  }
  SECTION("the defaults (> 0.5) light on a boolean") {
    const Template* t = builtins().find("dashboard/lamp");
    auto b = t->make({}, {});
    CHECK(lit(*b, 0) == 0.0);
    CHECK(lit(*b, 1) == 1.0);
  }
  SECTION("the op by its index when no name is given (as an expression sees an enum)") {
    const Template* t = builtins().find("dashboard/lamp");
    auto b = t->make({{"op", 2.0}, {"threshold", 0.0}}, {});  // "<"
    CHECK(lit(*b, -1) == 1.0);
    CHECK(lit(*b, 1) == 0.0);
  }
  SECTION("no reading lights nothing: unwired, NaN") {
    auto b = lamp("!=", 0);
    CHECK(get_or(b->step(Context{}, {}), "y", -1.0) == 0.0);
    CHECK(lit(*b, std::nan("")) == 0.0);
    auto below = lamp("<", 0);
    CHECK(lit(*below, std::nan("")) == 0.0);
  }
}

TEST_CASE("dashboard lamp: a latch holds the trip for the rest of the run") {
  auto b = lamp(">", 10, true);
  CHECK(lit(*b, 5) == 0.0);
  CHECK(lit(*b, 11) == 1.0);
  CHECK(lit(*b, 5) == 1.0);  // stays tripped
  CHECK(get_or(b->step(Context{}, {}), "y") == 1.0);
  auto fresh = lamp(">", 10, true);  // a new run makes a new lamp
  CHECK(lit(*fresh, 5) == 0.0);
}

TEST_CASE("dashboard lamp: a tuned threshold applies at once") {
  auto b = lamp(">", 10);
  CHECK(lit(*b, 8) == 0.0);
  CHECK(b->set_param("threshold", 5));
  CHECK(lit(*b, 8) == 1.0);
  CHECK_FALSE(b->set_param("latch", 1));
}
