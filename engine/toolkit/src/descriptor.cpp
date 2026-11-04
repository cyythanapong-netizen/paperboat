#include "paperboat/toolkit/descriptor.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

namespace paperboat::toolkit {
namespace {

// Descriptors are TOML. They are converted to this JSON tree, which the checks below
// and catalog_message work on; ordered so params and ports keep the file's order.
using json = nlohmann::ordered_json;

std::string at_line(const toml::source_region& where) {
  return "line " + std::to_string(where.begin.line);
}

json to_json(const toml::node& node) {
  if (const auto* table = node.as_table()) {
    // toml++ keeps keys sorted; put them back in the order they were written.
    std::vector<std::pair<const toml::key*, const toml::node*>> items;
    for (const auto& [key, value] : *table) items.emplace_back(&key, &value);
    std::ranges::sort(items, {}, [](const auto& kv) {
      const auto& pos = kv.first->source().begin;
      return std::pair{pos.line, pos.column};
    });
    json out = json::object();
    for (const auto& [key, value] : items) out[std::string(key->str())] = to_json(*value);
    return out;
  }
  if (const auto* array = node.as_array()) {
    json out = json::array();
    for (const auto& value : *array) out.push_back(to_json(value));
    return out;
  }
  if (const auto* v = node.as_string()) return v->get();
  if (const auto* v = node.as_integer()) return v->get();
  if (const auto* v = node.as_floating_point()) return v->get();
  if (const auto* v = node.as_boolean()) return v->get();
  throw DescriptorError(at_line(node.source()) + ": dates and times are not used in descriptors");
}

// A number param's value: a number, or true / false for a boolean.
double number_of(const json& v) { return v.is_boolean() ? (v.get<bool>() ? 1.0 : 0.0) : v.get<double>(); }

std::string where(std::size_t index, const json& entry) {
  std::string name = entry.value("type", entry.value("use", std::string{}));
  return "blocks[" + std::to_string(index) + "]" + (name.empty() ? "" : " (" + name + ")");
}

// Typos in a descriptor should fail loudly, not be ignored.
void only_fields(const json& obj, std::initializer_list<std::string_view> allowed, const std::string& at) {
  for (const auto& [key, _] : obj.items()) {
    if (std::ranges::find(allowed, key) == allowed.end()) throw DescriptorError(at + ": unknown field \"" + key + "\"");
  }
}

Config resolve_text(const BlockDef& def, const Config& config, const Config& text);

// New params a definition adds on an extendable parent: { k = 2, on = true, name = "x",
// key = { default = "Space", type = "key" } }: numbers, booleans, text, or a typed table.
std::vector<ParamDef> new_params(const json& obj, const std::string& at) {
  std::vector<ParamDef> out;
  for (const auto& [id, v] : obj.items()) {
    if (v.is_boolean()) out.push_back({.id = id, .default_value = v.get<bool>() ? 1.0 : 0.0, .type = "boolean"});
    else if (v.is_number()) out.push_back({.id = id, .default_value = v.get<double>()});
    else if (v.is_string()) out.push_back({.id = id, .type = "text", .default_text = v.get<std::string>()});
    else if (v.is_object()) {
      only_fields(v, {"default", "type", "options"}, at + ".params." + id);
      ParamDef p{.id = id, .type = v.value("type", std::string{"number"})};
      if (p.is_number()) p.default_value = number_of(v.at("default"));
      else p.default_text = v.at("default").get<std::string>();
      if (p.type != "number" && p.type != "boolean" && p.type != "text" && p.type != "key" && p.type != "file" && p.type != "enum")
        throw DescriptorError(at + ": param " + id + " has type \"" + p.type + "\" (number, boolean, text, key, file or enum)");
      if (p.type == "enum") {
        p.options = v.value("options", std::vector<std::string>{});
        if (p.options.empty()) throw DescriptorError(at + ": enum param " + id + " needs options");
        if (p.option(p.default_text) < 0)
          throw DescriptorError(at + ": enum param " + id + "'s default \"" + p.default_text + "\" isn't one of its options");
      }
      out.push_back(p);
    } else {
      throw DescriptorError(at + ": param " + id + " needs a default: a number, true / false, text or { default, type }");
    }
  }
  return out;
}

CatalogEntry from_template(const json& e, const std::string& at, const Registry& registry) {
  only_fields(e, {"use", "type", "label", "description", "params", "defaults", "config"}, at);
  const auto name = e.at("use").get<std::string>();
  const Template* t = registry.find(name);
  if (!t) {
    std::string known;
    for (const auto* k : registry.all()) known += (known.empty() ? "" : ", ") + k->name;
    throw DescriptorError(at + ": no template \"" + name + "\" (known: " + known + ")");
  }

  CatalogEntry entry{.def = t->def, .from = t, .config = t->config, .group = name.substr(0, name.find('/'))};
  entry.def.type = e.value("type", t->def.type);
  entry.def.label = e.value("label", t->def.label);
  entry.def.description = e.value("description", t->def.description);
  // A definition on a parent (TOML inheriting C++): its own params come after the parent's.
  if (e.contains("params")) {
    if (!t->extendable) throw DescriptorError(at + ": " + name + " takes no new params (it isn't a parent for definitions)");
    for (auto& p : new_params(e.at("params"), at)) {
      if (std::ranges::find(entry.def.params, p.id, &ParamDef::id) != entry.def.params.end())
        throw DescriptorError(at + ": param " + p.id + " is already " + name + "'s");
      entry.def.params.push_back(std::move(p));
    }
  }
  // value() returns a copy: keep it alive while iterating its items.
  const json defaults = e.value("defaults", json::object());
  const json config = e.value("config", json::object());
  for (const auto& [id, value] : defaults.items()) {
    auto p = std::ranges::find(entry.def.params, id, &ParamDef::id);
    if (p == entry.def.params.end()) throw DescriptorError(at + ": " + name + " has no param \"" + id + "\"");
    if (p->is_number()) p->default_value = number_of(value);
    else p->default_text = value.get<std::string>();
    if (p->type == "enum" && p->option(p->default_text) < 0)
      throw DescriptorError(at + ": " + id + " must be one of its options, not \"" + p->default_text + "\"");
  }
  for (const auto& [key, value] : config.items()) {
    if (!t->config.contains(key) && !t->extendable) throw DescriptorError(at + ": " + name + " has no config \"" + key + "\"");
    entry.config[key] = value.get<std::string>();
  }
  if (t->check) {
    try {
      t->check(entry.def, resolve_text(entry.def, entry.config, {}));
    } catch (const std::exception& err) {
      throw DescriptorError(at + ": " + err.what());
    }
  }
  return entry;
}

// A linked block's catalog entry: its station object acts for it; never in the palette.
CatalogEntry linked_entry(const ObjectDef& def, const std::string& group) {
  return {.def = *def.block, .from = nullptr, .config = {}, .group = group, .linked = true};
}

ObjectEntry object_from_template(const json& e, const std::string& at, const Registry& registry) {
  only_fields(e, {"use", "type", "label", "description", "defaults"}, at);
  const auto name = e.at("use").get<std::string>();
  const StationTemplate* t = registry.find_object(name);
  if (!t) throw DescriptorError(at + ": no station template \"" + name + "\"");
  ObjectEntry entry{.def = t->def, .from = t, .group = name.substr(0, name.find('/'))};
  entry.def.type = e.value("type", t->def.type);
  entry.def.label = e.value("label", t->def.label);
  entry.def.description = e.value("description", t->def.description);
  const json defaults = e.value("defaults", json::object());
  for (const auto& [id, value] : defaults.items()) {
    auto p = std::ranges::find(entry.def.props, id, &ParamDef::id);
    if (p == entry.def.props.end()) throw DescriptorError(at + ": " + name + " has no property \"" + id + "\"");
    if (p->is_number()) p->default_value = number_of(value);
    else p->default_text = value.get<std::string>();
  }
  return entry;
}

// [dashboard]: the default layout of the Dashboard's grid, in the protocol's shape. Blocks
// are not checked against the catalog (a layout may name monitors the user has not added;
// the UI ignores unknown ones), only its own shape.
void load_dashboard(const json& doc, Catalog& catalog) {
  if (!doc.contains("dashboard")) return;
  const json& d = doc.at("dashboard");
  only_fields(d, {"columns", "instruments"}, "dashboard");
  json out = json::object();
  if (d.contains("columns")) {
    if (!d["columns"].is_number_integer() || d["columns"] < 1 || d["columns"] > 12)
      throw DescriptorError("dashboard.columns must be a whole number from 1 to 12");
    out["columns"] = d["columns"];
  }
  json instruments = json::array();
  std::set<std::string> seen;
  const json list = d.value("instruments", json::array());
  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& e = list[i];
    const auto at = "dashboard.instruments[" + std::to_string(i) + "]";
    only_fields(e, {"block", "size", "at"}, at);
    if (!e.contains("block") || !e["block"].is_string()) throw DescriptorError(at + ": needs \"block\", a monitor block's id");
    if (!seen.insert(e["block"].get<std::string>()).second)
      throw DescriptorError(at + ": block \"" + e["block"].get<std::string>() + "\" is placed twice");
    json item = {{"block", e["block"]}};
    if (e.contains("size")) {
      const std::string size = e["size"].is_string() ? e["size"].get<std::string>() : "";
      if (size != "compact" && size != "normal" && size != "large" && size != "xlarge")
        throw DescriptorError(at + ": size must be compact, normal, large or xlarge");
      item["size"] = size;
    }
    if (e.contains("at")) {
      const auto& a = e["at"];
      if (!a.is_array() || a.size() != 2 || !a[0].is_number_integer() || !a[1].is_number_integer() || a[0] < 0 || a[1] < 0)
        throw DescriptorError(at + ": at must be [column, row], whole numbers from 0");
      item["at"] = a;
    }
    instruments.push_back(std::move(item));
  }
  out["instruments"] = std::move(instruments);
  catalog.dashboard = out.dump();
}

// [graph] and [station]: the starters, in the protocol's shapes, checked against the
// catalog so a typo fails at load, not when the UI opens.
void load_starters(const json& doc, Catalog& catalog) {
  json graph = doc.value("graph", json::object());
  only_fields(graph, {"nodes", "edges", "groups"}, "graph");
  const json groups = graph.value("groups", json::array());
  graph = {{"nodes", graph.value("nodes", json::array())}, {"edges", graph.value("edges", json::array())}};
  std::set<std::string> ids;
  for (std::size_t i = 0; i < graph["nodes"].size(); ++i) {
    auto& n = graph["nodes"][i];
    const auto at = "graph.nodes[" + std::to_string(i) + "]";
    only_fields(n, {"id", "type", "params", "pos"}, at);
    if (!n.contains("id") || !n.contains("type")) throw DescriptorError(at + ": needs an id and a type");
    const auto type = n["type"].get<std::string>();
    if (!catalog.find(type)) throw DescriptorError(at + ": no block type \"" + type + "\" in this catalog");
    if (!ids.insert(n["id"].get<std::string>()).second) throw DescriptorError(at + ": duplicate id");
    if (!n.contains("params")) n["params"] = json::object();
  }
  // Groups: a visual frame, or a story (one step of a Step flow). Its id is also a node id
  // for step wires (its ports: in, next).
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto& g = groups[i];
    const auto at = "graph.groups[" + std::to_string(i) + "]";
    only_fields(g, {"id", "kind", "label", "nodes", "collapsed", "pos", "params"}, at);
    if (!g.contains("id") || !g.contains("nodes")) throw DescriptorError(at + ": needs an id and nodes");
    const auto kind = g.value("kind", std::string{"visual"});
    if (kind != "visual" && kind != "story") throw DescriptorError(at + ": kind must be visual or story");
    if (!ids.insert(g["id"].get<std::string>()).second) throw DescriptorError(at + ": duplicate id");
    for (const auto& n : g["nodes"])
      if (!ids.contains(n.get<std::string>())) throw DescriptorError(at + ": no node \"" + n.get<std::string>() + "\"");
  }
  for (std::size_t i = 0; i < graph["edges"].size(); ++i) {
    const auto& e = graph["edges"][i];
    const auto at = "graph.edges[" + std::to_string(i) + "]";
    only_fields(e, {"from", "to"}, at);
    for (const char* end : {"from", "to"}) {
      if (!e.contains(end) || e[end].size() != 2) throw DescriptorError(at + ": " + end + " must be [node, port]");
      if (!ids.contains(e[end][0].get<std::string>()))
        throw DescriptorError(at + ": no node \"" + e[end][0].get<std::string>() + "\"");
    }
  }
  if (!groups.empty()) graph["groups"] = groups;
  catalog.graph = graph.dump();

  if (!doc.contains("station")) return;
  json station = doc.at("station");
  only_fields(station, {"view", "page", "objects"}, "station");
  catalog.station_view = station.value("view", std::string{"scene"});
  catalog.station_page = station.value("page", std::string{});
  if (catalog.station_view != "scene" && catalog.station_view != "graph" && catalog.station_view != "web")
    throw DescriptorError("station.view must be scene, graph or web, not \"" + catalog.station_view + "\"");
  if ((catalog.station_view == "web") != !catalog.station_page.empty())
    throw DescriptorError("station: a web station needs a page (and only a web station has one)");
  if (catalog.station_page.find("..") != std::string::npos || catalog.station_page.starts_with('/'))
    throw DescriptorError("station.page must be a path inside the backend's folder");
  station = {{"objects", station.value("objects", json::array())}};
  for (std::size_t i = 0; i < station["objects"].size(); ++i) {
    auto& o = station["objects"][i];
    const auto at = "station.objects[" + std::to_string(i) + "]";
    only_fields(o, {"id", "type", "props", "links", "pos"}, at);
    if (!o.contains("id") || !o.contains("type")) throw DescriptorError(at + ": needs an id and a type");
    if (!catalog.find_object(o["type"].get<std::string>()))
      throw DescriptorError(at + ": no station object type \"" + o["type"].get<std::string>() + "\"");
    if (!o.contains("props")) o["props"] = json::object();
  }
  std::set<std::string> object_ids;
  for (const auto& o : station["objects"]) object_ids.insert(o["id"].get<std::string>());
  for (const auto& o : station["objects"]) {
    const ObjectEntry* entry = catalog.find_object(o["type"].get<std::string>());
    const auto& slots = entry->def.links;
    const json links = o.value("links", json::object());  // a copy: keep it alive while iterating
    for (const auto& [slot, target] : links.items()) {
      const auto at = "station object " + o["id"].get<std::string>();
      if (std::ranges::find(slots, slot) == slots.end()) throw DescriptorError(at + ": no link slot \"" + slot + "\"");
      if (!object_ids.contains(target.get<std::string>()))
        throw DescriptorError(at + ": link " + slot + " names " + target.get<std::string>() + ", which isn't on the station");
    }
  }
  catalog.station = station.dump();
}

Values resolve(const BlockDef& def, const Values& params) {
  Values resolved;
  for (const auto& p : def.params) {
    if (p.is_number()) resolved[p.id] = get_or(params, p.id, p.default_value);
  }
  return resolved;
}

// The entry's config plus its text params, given or default.
Config resolve_text(const BlockDef& def, const Config& config, const Config& text) {
  Config resolved = config;
  for (const auto& p : def.params) {
    if (p.is_number()) continue;
    auto it = text.find(p.id);
    resolved[p.id] = it == text.end() ? p.default_text : it->second;
  }
  return resolved;
}

}  // namespace

namespace {

// Numbers for every number param, and each enum's index in its options (an enum's name
// is in `text`). A name that isn't an option is an error.
Values with_enums(const BlockDef& def, Values values, const Config& text) {
  for (const auto& p : def.params) {
    if (p.type != "enum") continue;
    const auto name = text.at(p.id);
    const int i = p.option(name);
    if (i < 0) {
      std::string known;
      for (const auto& o : p.options) known += (known.empty() ? "" : ", ") + o;
      throw std::invalid_argument(p.id + " is \"" + name + "\", not one of: " + known);
    }
    values[p.id] = static_cast<double>(i);
  }
  return values;
}

}  // namespace

std::unique_ptr<Block> CatalogEntry::make(const Values& params, const Config& text) const {
  if (!from) throw std::logic_error("\"" + def.type + "\" is a linked block: its station object acts for it");
  if (!from->make) throw std::logic_error("\"" + def.type + "\" is a step block: use make()");
  const Config t = resolve_text(def, config, text);
  return from->make(with_enums(def, resolve(def, params), t), t);
}

std::unique_ptr<Step> CatalogEntry::make_step(const Values& params, const Config& text) const {
  if (!from) throw std::logic_error("\"" + def.type + "\" is a linked block: its station object acts for it");
  if (!from->make_step) throw std::logic_error("\"" + def.type + "\" is a signal block: use make()");
  const Config t = resolve_text(def, config, text);
  return from->make_step(with_enums(def, resolve(def, params), t), t);
}

std::unique_ptr<StationObject> ObjectEntry::make(const Values& props) const {
  Values resolved;
  for (const auto& p : def.props) {
    if (p.is_number()) resolved[p.id] = get_or(props, p.id, p.default_value);
    else resolved[p.id] = get_text(props, p.id, p.default_text);
  }
  return from->make(resolved);
}

const ObjectEntry* Catalog::find_object(std::string_view type) const {
  auto it = std::ranges::find(objects, type, [](const ObjectEntry& e) -> const std::string& { return e.def.type; });
  return it == objects.end() ? nullptr : &*it;
}

const CatalogEntry* Catalog::find(std::string_view type) const {
  auto it = std::ranges::find(entries, type, [](const CatalogEntry& e) -> const std::string& { return e.def.type; });
  return it == entries.end() ? nullptr : &*it;
}

Catalog load_descriptor(std::string_view toml_text, const Registry& registry) {
  json doc;
  try {
    doc = to_json(toml::parse(toml_text));
  } catch (const toml::parse_error& err) {
    throw DescriptorError("not valid TOML: " + at_line(err.source()) + ": " + std::string(err.description()));
  }
  only_fields(doc, {"domain", "import", "blocks", "objects", "graph", "station", "dashboard"}, "descriptor");

  Catalog catalog;
  catalog.domain = doc.value("domain", std::string{});
  const json blocks = doc.value("blocks", json::array());
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& e = blocks[i];
    const auto at = where(i, e);
    try {
      if (!e.contains("use"))
        throw DescriptorError(at + ": needs \"use\": a domain defines its own blocks in code and registers them "
                                   "(GUIDELINES §6); a descriptor only picks and customises templates");
      auto entry = from_template(e, at, registry);
      if (e.contains("params")) entry.group = catalog.domain;  // a definition of the domain's own
      if (catalog.find(entry.def.type)) throw DescriptorError(at + ": duplicate type \"" + entry.def.type + "\"");
      catalog.entries.push_back(std::move(entry));
    } catch (const json::exception& err) {
      throw DescriptorError(at + ": " + err.what());
    }
  }

  // Station objects, and the linked block each one spawns.
  auto add_object = [&](ObjectEntry object, const std::string& at) {
    if (catalog.find_object(object.def.type)) throw DescriptorError(at + ": duplicate object type \"" + object.def.type + "\"");
    if (object.def.block) {
      if (catalog.find(object.def.block->type))
        throw DescriptorError(at + ": linked block type \"" + object.def.block->type + "\" is already a block");
      catalog.entries.push_back(linked_entry(object.def, object.group));
    }
    catalog.objects.push_back(std::move(object));
  };
  const json objects = doc.value("objects", json::array());
  for (std::size_t i = 0; i < objects.size(); ++i) {
    const auto& e = objects[i];
    const auto at = "objects[" + std::to_string(i) + "]" +
                    (e.contains("use") || e.contains("type") ? " (" + e.value("type", e.value("use", std::string{})) + ")" : "");
    try {
      if (!e.contains("use"))
        throw DescriptorError(at + ": needs \"use\": a domain defines its own station objects in code (GUIDELINES §6)");
      add_object(object_from_template(e, at, registry), at);
    } catch (const json::exception& err) {
      throw DescriptorError(at + ": " + err.what());
    }
  }

  // Whole packages: every template under its default type, unless an entry above already
  // has that type (a customised `use` wins). Station templates come along too.
  // The domain's own package (templates registered in code under "<domain>/…") comes first,
  // without being listed.
  std::vector<std::string> group_order{catalog.domain};
  std::vector<std::string> imports;
  if (!catalog.domain.empty() && (!registry.package(catalog.domain).empty() || !registry.objects(catalog.domain).empty()))
    imports.push_back(catalog.domain);
  for (const auto& name : doc.value("import", json::array())) {
    if (name.get<std::string>() != catalog.domain) imports.push_back(name.get<std::string>());
  }
  for (const auto& package : imports) {
    const auto templates = registry.package(package);
    const auto station = registry.objects(package);
    if (templates.empty() && station.empty()) {
      std::string known;
      for (const auto& k : registry.packages()) known += (known.empty() ? "" : ", ") + k;
      throw DescriptorError("import: no package \"" + package + "\" (known: " + known + ")");
    }
    if (package != catalog.domain) group_order.push_back(package);
    for (const Template* t : templates) {
      if (t->abstract || catalog.find(t->def.type)) continue;  // a parent only
      catalog.entries.push_back({.def = t->def, .from = t, .config = t->config, .group = package});
    }
    for (const StationTemplate* t : station) {
      if (catalog.find_object(t->def.type)) continue;
      add_object({.def = t->def, .from = t, .group = package}, "import " + package);
    }
  }
  // Palette order: the domain's own blocks, then each imported package in import order
  // (customised entries join their package), then packages used but not imported.
  auto rank = [&](const CatalogEntry& e) {
    auto it = std::ranges::find(group_order, e.group);
    return it == group_order.end() ? group_order.size() : static_cast<std::size_t>(it - group_order.begin());
  };
  std::ranges::stable_sort(catalog.entries, {}, rank);
  std::ranges::stable_sort(catalog.objects, {}, [&](const ObjectEntry& o) {
    auto it = std::ranges::find(group_order, o.group);
    return it == group_order.end() ? group_order.size() : static_cast<std::size_t>(it - group_order.begin());
  });
  if (catalog.entries.empty() && catalog.objects.empty())
    throw DescriptorError("descriptor: the catalog is empty: import packages, use templates, or register the domain's "
                          "own under the package \"" + catalog.domain + "\"");
  load_dashboard(doc, catalog);
  load_starters(doc, catalog);
  return catalog;
}

void define_blocks(Registry& registry, const std::string& package, std::string_view toml_text) {
  json doc;
  try {
    doc = to_json(toml::parse(toml_text));
  } catch (const toml::parse_error& err) {
    throw DescriptorError(package + " definitions: not valid TOML: " + at_line(err.source()) + ": " + std::string(err.description()));
  }
  only_fields(doc, {"blocks"}, package + " definitions");
  const json blocks = doc.value("blocks", json::array());
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto at = package + " " + where(i, blocks[i]);
    if (!blocks[i].contains("use") || !blocks[i].contains("type")) throw DescriptorError(at + ": needs \"use\" (its parent) and \"type\"");
    const auto entry = from_template(blocks[i], at, registry);
    Template child = *entry.from;  // the parent's behaviour…
    child.name = package + "/" + entry.def.type;
    child.def = entry.def;         // …with this definition
    child.config = entry.config;
    child.abstract = false;
    registry.add(std::move(child));
  }
}

std::string descriptor_domain(std::string_view toml_text) {
  try {
    const auto doc = toml::parse(toml_text);
    return doc["domain"].value_or(std::string{});
  } catch (const toml::parse_error& err) {
    throw DescriptorError("not valid TOML: " + at_line(err.source()) + ": " + std::string(err.description()));
  }
}

Catalog load_descriptor_file(const std::filesystem::path& path, const Registry& registry) {
  std::ifstream in(path);
  if (!in) throw DescriptorError("cannot open " + path.string());
  std::stringstream text;
  text << in.rdbuf();
  Catalog catalog = load_descriptor(text.str(), registry);
  catalog.dir = std::filesystem::absolute(path).parent_path();
  if (!catalog.station_page.empty() && !std::filesystem::is_regular_file(catalog.dir / catalog.station_page))
    throw DescriptorError("station.page: no file " + (catalog.dir / catalog.station_page).string());
  return catalog;
}

std::string catalog_message(const Catalog& catalog, int indent) {
  auto ports = [](const std::vector<PortDef>& list) {
    json out = json::array();
    for (const auto& p : list) out.push_back({{"id", p.id}, {"type", p.type}});
    return out;
  };
  auto param_list = [](const std::vector<ParamDef>& list) {
    json out = json::array();
    for (const auto& p : list) {
      if (p.type == "number") out.push_back({{"id", p.id}, {"type", "number"}, {"default", p.default_value}});
      else if (p.type == "boolean") out.push_back({{"id", p.id}, {"type", "boolean"}, {"default", p.default_value != 0}});
      else if (p.type == "enum") out.push_back({{"id", p.id}, {"type", "enum"}, {"default", p.default_text}, {"options", p.options}});
      else out.push_back({{"id", p.id}, {"type", p.type}, {"default", p.default_text}});
    }
    return out;
  };
  json blocks = json::array();
  for (const auto& e : catalog.entries) {
    json params = param_list(e.def.params);
    json block = {{"type", e.def.type},
                  {"label", e.def.label},
                  {"kind", to_string(e.def.kind)},
                  {"inputs", ports(e.def.inputs)},
                  {"outputs", ports(e.def.outputs)},
                  {"params", params}};
    if (!e.group.empty()) block["group"] = e.group;
    if (e.linked) block["linked"] = true;
    if (!e.def.instrument.empty()) block["instrument"] = e.def.instrument;
    if (!e.def.description.empty()) block["description"] = e.def.description;
    if (!e.def.feedthrough) block["feedthrough"] = false;
    if (!e.def.forms.empty()) {
      json forms = json::array();
      for (const auto& f : e.def.forms)
        forms.push_back({{"name", f.name}, {"inputs", ports(f.inputs)}, {"outputs", ports(f.outputs)}});
      block["forms"] = std::move(forms);
    }
    blocks.push_back(std::move(block));
  }
  json msg = {{"type", "catalog"}, {"blocks", blocks}};
  if (!catalog.dashboard.empty()) msg["dashboard"] = json::parse(catalog.dashboard);
  if (!catalog.objects.empty()) {
    json objects = json::array();
    for (const auto& o : catalog.objects) {
      json object = {{"type", o.def.type}, {"label", o.def.label}, {"role", o.def.role},
                     {"props", param_list(o.def.props)}, {"links", o.def.links}};
      if (!o.group.empty()) object["group"] = o.group;
      if (o.def.block) object["block"] = o.def.block->type;
      if (!o.def.description.empty()) object["description"] = o.def.description;
      objects.push_back(std::move(object));
    }
    msg["objects"] = std::move(objects);
  }
  return msg.dump(indent);
}

}  // namespace paperboat::toolkit
