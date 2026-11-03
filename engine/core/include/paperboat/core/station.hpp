#pragma once

// A station from the protocol's JSON: its objects, made from a catalog, and their scene.

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <paperboat/toolkit/descriptor.hpp>

namespace paperboat::core {

using json = nlohmann::json;

// A station or graph that doesn't fit the catalog. Fails the run, never the backend.
// `node` names the block or station object at fault, when there is one: the UI marks it.
struct GraphError : std::runtime_error {
  using std::runtime_error::runtime_error;
  GraphError(std::string node_id, const std::string& what) : std::runtime_error(what), node(std::move(node_id)) {}
  std::string node;
};

struct StationItem {
  std::string id;
  const toolkit::ObjectEntry* entry = nullptr;
  std::unique_ptr<toolkit::StationObject> instance;
};
using Station = std::vector<StationItem>;

// {"objects": [{id, type, props, links}]} → instances, linked to each other. Throws
// GraphError (unknown type, duplicate id, a bad link) or json::exception (malformed).
Station make_station(const json& station, const toolkit::Catalog& catalog);

// Every object's entities as the protocol's Entity list, objects side by side in depth.
json scene_of(const Station& station);

// A number param's (or prop's) value in a graph or station: its number, true / false as
// 1 / 0, or `fallback` when absent.
double number_param(const json& given, const std::string& id, double fallback);

// One entity as the protocol's JSON.
json entity_json(const toolkit::Entity& e);

// What blocks draw on the Station (results they show).
json scene_of_blocks(const std::vector<const toolkit::Block*>& blocks);

}  // namespace paperboat::core
