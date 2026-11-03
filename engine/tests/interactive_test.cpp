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
