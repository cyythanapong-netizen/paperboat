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

TEST_CASE("the workbench has the standard toolkits and only optional station objects") {
  const auto& catalog = paperboat::workbench::catalog();
  std::set<std::string> groups;
  for (const auto& entry : catalog.entries) groups.insert(entry.group);
  for (const char* package : {"base", "math", "signal", "dashboard"}) CHECK(groups.contains(package));
#ifdef PAPERBOAT_HAS_CONTROL
  CHECK(groups.contains("control"));
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
  CHECK(groups.contains("optimization"));
#endif
#ifdef PAPERBOAT_HAS_OPENCV
  CHECK(groups.contains("opencv"));
#endif
#ifdef PAPERBOAT_HAS_MESHOP
  CHECK(groups.contains("meshop"));
#endif
  CHECK(catalog.missing_imports.empty());
  // The station objects come from the optional packages that are there (linked in or loaded as
  // plugins), and nothing else.
  std::set<std::string> objects;
  for (const auto& object : catalog.objects) objects.insert(object.def.type);
  std::set<std::string> expected;
  if (groups.contains("opencv")) expected.insert("image");
  if (groups.contains("meshop")) expected.insert("mesh");
  CHECK(objects == expected);
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
