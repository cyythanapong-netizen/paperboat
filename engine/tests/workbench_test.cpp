#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "paperboat/core/run.hpp"
#include "paperboat/toolkit/registry.hpp"
#include "workbench.hpp"

namespace {

using nlohmann::json;
namespace core = paperboat::core;

// Every message a run sends, kept in order.
struct Collected {
  std::vector<json> messages;
  core::Emit emit() {
    return [this](const json& message) { messages.push_back(message); };
  }
  std::vector<double> signals(const std::string& node) const {
    std::vector<double> values;
    for (const auto& m : messages)
      if (m["type"] == "signal" && m["node"] == node) values.push_back(m["v"].get<double>());
    return values;
  }
};

}  // namespace

TEST_CASE("the workbench has the standard toolkits and no station objects") {
  const auto& catalog = paperboat::workbench::catalog();
  std::set<std::string> groups;
  for (const auto& entry : catalog.entries) groups.insert(entry.group);
  for (const char* package : {"base", "math", "signal", "dashboard"}) CHECK(groups.contains(package));
  // control is an optional toolkit: in the catalog when installed, left out when not.
  const bool has_control = !paperboat::toolkit::builtins().package("control").empty();
  CHECK(groups.contains("control") == has_control);
  CHECK(catalog.missing_imports.empty());
  CHECK(catalog.objects.empty());
  CHECK(catalog.station_view == "graph");  // no domain scene to draw
  CHECK_FALSE(catalog.station_visible);    // and nothing to show there
}

TEST_CASE("the workbench starter runs its Sine into a Graph") {
  const auto& backend = paperboat::workbench::backend();
  Collected out;
  const auto status = core::run("workbench-1", json::parse(backend.catalog->graph), nullptr, backend, out.emit(),
                                nullptr, std::stop_token{}, json{{"speed", 0}, {"duration", 0.5}});
  CHECK(status == "done");
  CHECK(out.signals("graph").size() > 10);
}
