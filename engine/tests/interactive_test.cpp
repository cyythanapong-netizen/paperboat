#include <catch2/catch_test_macros.hpp>

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

  // Toggle mode (mode = 0), initial = 1.0, key = "btn"
  auto toggle_sw = t->make({{"mode", 0.0}, {"initial", 1.0}}, {{"key", "btn"}});
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

  // Momentary mode (mode = 1), default node name as key fallback
  auto mom_sw = t->make({{"mode", 1.0}, {"initial", 0.0}}, {});
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

