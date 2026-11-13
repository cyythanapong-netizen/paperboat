#include <catch2/catch_test_macros.hpp>

#include "paperboat/toolkit/plugin.hpp"

namespace tk = paperboat::toolkit;

TEST_CASE("plugins: one that matches registers, another ABI is refused, a package already there is skipped") {
  tk::Registry r = tk::builtins();
  const auto loads = tk::load_plugins(r, {PAPERBOAT_TEST_PLUGINS});
  REQUIRE(loads.size() == 2);
  for (const auto& l : loads) {
    if (l.name == "plugintest") {
      CHECK(l.ok);
    } else {
      CHECK(l.name == "badabi");
      CHECK_FALSE(l.ok);
      CHECK(l.message.find("paperboat 0.0.0, libnope") != std::string::npos);
    }
  }
  CHECK(r.find("plugintest/one") != nullptr);
  CHECK(tk::load_plugins(r, {PAPERBOAT_TEST_PLUGINS}).size() == 1);  // plugintest is there now: only badabi again
}
