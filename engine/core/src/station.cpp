#include "paperboat/core/station.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace paperboat::core {

namespace {
constexpr double kLane = 1.2;  // m between station objects in depth, so they don't overlap
}

Station make_station(const json& station, const toolkit::Catalog& catalog) {
  Station out;
  if (station.is_null()) return out;
  std::set<std::string> ids;
  for (const auto& o : station.at("objects")) {
    const auto id = o.at("id").get<std::string>();
    const auto type = o.at("type").get<std::string>();
    if (!ids.insert(id).second) throw GraphError("two station objects are called " + id);
    const auto* entry = catalog.find_object(type);
    if (!entry) throw GraphError("unknown station object type: " + type);
    if (!entry->from) throw GraphError("no implementation for station object type: " + type);
    const json given = o.value("props", json::object());
    toolkit::Values props;
    for (const auto& p : entry->def.props) {
      if (p.is_number()) {
        props[p.id] = number_param(given, p.id, p.default_value);
      } else {
        std::string text = given.value(p.id, p.default_text);
        // A file is named relative to the descriptor, so a backend runs from anywhere.
        if (p.type == "file" && !text.empty() && std::filesystem::path(text).is_relative())
          text = (catalog.dir / text).string();
        props[p.id] = text;
      }
    }
    out.push_back({.id = id, .entry = entry, .instance = entry->make(props)});
  }
  // Links ({"slot": "other id"}), once every object exists: each slot the type declares,
  // each target an object of this station.
  for (const auto& o : station.at("objects")) {
    if (!o.contains("links")) continue;
    auto& item = *std::ranges::find(out, o.at("id").get<std::string>(), &StationItem::id);
    for (const auto& [slot, target] : o.at("links").items()) {
      if (std::ranges::find(item.entry->def.links, slot) == item.entry->def.links.end())
        throw GraphError(item.id, item.id + " (" + item.entry->def.type + ") has no link \"" + slot + "\"");
      const auto other = std::ranges::find(out, target.get<std::string>(), &StationItem::id);
      if (other == out.end())
        throw GraphError(item.id, item.id + "." + slot + " links to " + target.get<std::string>() + ", which isn't on the station");
      if (other->id == item.id) throw GraphError(item.id, item.id + "." + slot + " links to itself");
      item.instance->link(slot, *other->instance);
    }
  }
  return out;
}

double number_param(const json& given, const std::string& id, double fallback) {
  auto it = given.find(id);
  if (it == given.end() || it->is_null()) return fallback;
  return it->is_boolean() ? (it->get<bool>() ? 1.0 : 0.0) : it->get<double>();
}

json entity_json(const toolkit::Entity& e) {
  json out = {{"id", e.id}, {"shape", e.shape}, {"pos", e.pos}, {"size", e.size}, {"color", e.color}};
  if (e.shape == "line") out["to"] = e.to;
  if (e.shape == "image") out["src"] = e.src;
  if (e.rot != std::array<double, 3>{}) out["rot"] = e.rot;
  if (e.scale != std::array<double, 3>{1, 1, 1}) out["scale"] = e.scale;
  if (!e.vertices.empty()) out["vertices"] = e.vertices;
  if (!e.indices.empty()) out["indices"] = e.indices;
  if (!e.values.empty()) out["values"] = e.values;
  if (e.range != std::array<double, 2>{}) out["range"] = e.range;
  return out;
}

json scene_of(const Station& station) {
  json out = json::array();
  for (std::size_t i = 0; i < station.size(); ++i) {
    for (const auto& e : station[i].instance->entities(station[i].id, -kLane * static_cast<double>(i))) {
      out.push_back(entity_json(e));
    }
  }
  return out;
}

json scene_of_blocks(const std::vector<const toolkit::Block*>& blocks) {
  json out = json::array();
  for (const auto* b : blocks)
    for (const auto& e : b->entities()) out.push_back(entity_json(e));
  return out;
}

}  // namespace paperboat::core
