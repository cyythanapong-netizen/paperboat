#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/variables.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <map>
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
      only_fields(v, {"default", "type", "options", "unit"}, at + ".params." + id);
      ParamDef p{.id = id, .type = v.value("type", std::string{"number"})};
      p.unit = v.value("unit", std::string{});
      if (p.is_number()) p.default_value = number_of(v.at("default"));
      else p.default_text = v.at("default").get<std::string>();
      if (p.type != "number" && p.type != "boolean" && p.type != "text" && p.type != "key" && p.type != "file" && p.type != "enum" && p.type != "variable")
        throw DescriptorError(at + ": param " + id + " has type \"" + p.type + "\" (number, boolean, text, key, file, enum or variable)");
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

// A `view` table: compact, glyph, value, icon (all optional).
View view_from(const json& v, const std::string& at) {
  only_fields(v, {"compact", "glyph", "value", "icon"}, at);
  View out;
  if (v.contains("compact")) {
    if (!v["compact"].is_boolean()) throw DescriptorError(at + ".compact must be true or false");
    out.compact = v["compact"].get<bool>();
  }
  for (const char* key : {"glyph", "value"}) {
    if (!v.contains(key)) continue;
    if (!v[key].is_string()) throw DescriptorError(at + "." + key + " must be text");
    (std::string(key) == "glyph" ? out.glyph : out.value) = v[key].get<std::string>();
  }
  if (v.contains("icon")) {
    if (!v["icon"].is_object()) throw DescriptorError(at + ".icon must be a table");
    out.icon = nlohmann::json::parse(v["icon"].dump());
  }
  return out;
}

// `units = { force = "N", kp = "N/rad" }`: the units of a block's ports and number params
// (or a station object's props), by id. A wire between two ports with units must keep them.
void apply_units(const json& e, std::vector<ParamDef>& params, BlockDef* block, const std::string& at, const std::string& name) {
  if (!e.contains("units")) return;
  const json units = e.at("units");  // a copy: keep it alive while iterating
  if (!units.is_object()) throw DescriptorError(at + ": units is a table: { port or param = \"unit\" }");
  for (const auto& [id, value] : units.items()) {
    if (!value.is_string()) throw DescriptorError(at + ": units." + id + " is text, such as \"N\"");
    const auto unit = value.get<std::string>();
    bool found = false;
    if (auto p = std::ranges::find(params, id, &ParamDef::id); p != params.end()) {
      if (p->type != "number") throw DescriptorError(at + ": " + id + " is a " + p->type + " param: only numbers have units");
      p->unit = unit;
      found = true;
    }
    if (block)
      for (auto* ports : {&block->inputs, &block->outputs})
        if (auto port = std::ranges::find(*ports, id, &PortDef::id); port != ports->end()) {
          if (port->type == "step") throw DescriptorError(at + ": " + id + " is a step port: it carries no value, so no unit");
          port->unit = unit;
          found = true;
        }
    if (!found) throw DescriptorError(at + ": " + name + " has no port or param \"" + id + "\" (units)");
  }
}

CatalogEntry from_template(const json& e, const std::string& at, const Registry& registry) {
  only_fields(e, {"use", "type", "label", "description", "params", "defaults", "config", "view", "units"}, at);
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
  // The look is inherited from the template; a `view` overrides single keys of it.
  if (e.contains("view")) entry.def.view = entry.def.view.overridden_by(view_from(e.at("view"), at + ".view"));
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
  if (t->name == "signal/generator") {
    if (auto f = std::ranges::find(entry.def.params, std::string("f"), &ParamDef::id); f != entry.def.params.end() && f->type == "text")
      entry.def.inputs_from = "f";
  }
  if (!entry.def.inputs_from.empty()) {
    if (auto p = std::ranges::find(entry.def.params, entry.def.inputs_from, &ParamDef::id); p != entry.def.params.end())
      entry.def = with_inputs(entry.def, p->default_text);
  }
  apply_units(e, entry.def.params, &entry.def, at, name);
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
  only_fields(e, {"use", "type", "label", "description", "defaults", "units", "view"}, at);
  const auto name = e.at("use").get<std::string>();
  const StationTemplate* t = registry.find_object(name);
  if (!t) throw DescriptorError(at + ": no station template \"" + name + "\"");
  ObjectEntry entry{.def = t->def, .from = t, .group = name.substr(0, name.find('/'))};
  entry.def.type = e.value("type", t->def.type);
  entry.def.label = e.value("label", t->def.label);
  entry.def.description = e.value("description", t->def.description);
  if (e.contains("view")) {
    if (!entry.def.block) throw DescriptorError(at + ": " + name + " has no linked block for view");
    entry.def.block->view = entry.def.block->view.overridden_by(view_from(e.at("view"), at + ".view"));
  }
  const json defaults = e.value("defaults", json::object());
  for (const auto& [id, value] : defaults.items()) {
    auto p = std::ranges::find(entry.def.props, id, &ParamDef::id);
    if (p == entry.def.props.end()) throw DescriptorError(at + ": " + name + " has no property \"" + id + "\"");
    if (p->is_number()) p->default_value = number_of(value);
    else p->default_text = value.get<std::string>();
  }
  // Units of its props and of its linked block's ports.
  apply_units(e, entry.def.props, entry.def.block ? &*entry.def.block : nullptr, at, name);
  return entry;
}

// [dashboard]: the default layout of the Dashboard's grid, in the protocol's shape. Blocks
// are not checked against the catalog (a layout may name monitors the user has not added;
// the UI ignores unknown ones), only its own shape.
void load_dashboard(const json& doc, Catalog& catalog) {
  if (!doc.contains("dashboard")) return;
  const json& d = doc.at("dashboard");
  only_fields(d, {"columns", "tabs", "instruments", "decorations"}, "dashboard");
  json out = json::object();
  int columns = 6;  // the UI's default when none is given
  if (d.contains("columns")) {
    if (!d["columns"].is_number_integer() || d["columns"] < 1 || d["columns"] > 12)
      throw DescriptorError("dashboard.columns must be a whole number from 1 to 12");
    out["columns"] = d["columns"];
    columns = d["columns"].get<int>();
  }
  // Tabs: pages of the Dashboard. An instrument or decoration may name the tab it is on.
  std::set<std::string> tabs;
  if (d.contains("tabs")) {
    if (!d["tabs"].is_array()) throw DescriptorError("dashboard.tabs must be a list of names");
    for (const auto& t : d["tabs"]) {
      if (!t.is_string() || t.get<std::string>().empty()) throw DescriptorError("dashboard.tabs: each tab needs a name");
      if (!tabs.insert(t.get<std::string>()).second) throw DescriptorError("dashboard.tabs: \"" + t.get<std::string>() + "\" is twice");
    }
    out["tabs"] = d["tabs"];
  }
  const auto check_tab = [&](const json& e, const std::string& at) {
    if (!e.contains("tab")) return;
    if (!e["tab"].is_string() || !tabs.contains(e["tab"].get<std::string>()))
      throw DescriptorError(at + ": tab must be one of dashboard.tabs" + (tabs.empty() ? " (there are none)" : ""));
  };
  const auto check_at = [&](const json& a, const std::string& at) {
    if (!a.is_array() || a.size() != 2 || !a[0].is_number_integer() || !a[1].is_number_integer() || a[0] < 0 || a[1] < 0)
      throw DescriptorError(at + ": at must be [column, row], whole numbers from 0");
  };
  json instruments = json::array();
  std::set<std::string> seen;
  const json list = d.value("instruments", json::array());
  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& e = list[i];
    const auto at = "dashboard.instruments[" + std::to_string(i) + "]";
    only_fields(e, {"block", "size", "at", "tab", "settings"}, at);
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
      check_at(e["at"], at);
      item["at"] = e["at"];
    }
    check_tab(e, at);
    if (e.contains("tab")) item["tab"] = e["tab"];
    if (e.contains("settings")) {  // an instrument's own settings: the toolkit's UI reads them; only the shape is checked here
      if (!e["settings"].is_object()) throw DescriptorError(at + ": settings must be a table");
      item["settings"] = e["settings"];
    }
    instruments.push_back(std::move(item));
  }
  out["instruments"] = std::move(instruments);

  // Decorations: a section (a titled frame), hline and vline (separators), a label (text), the console (#76).
  // They belong to the layout: the Flow still decides which instruments exist.
  if (d.contains("decorations")) {
    if (!d["decorations"].is_array()) throw DescriptorError("dashboard.decorations must be a list");
    json decorations = json::array();
    const auto& dlist = d["decorations"];
    for (std::size_t i = 0; i < dlist.size(); ++i) {
      const auto& e = dlist[i];
      const auto at = "dashboard.decorations[" + std::to_string(i) + "]";
      only_fields(e, {"kind", "text", "at", "span", "style", "tab"}, at);
      const std::string kind = e.contains("kind") && e["kind"].is_string() ? e["kind"].get<std::string>() : "";
      if (kind != "section" && kind != "hline" && kind != "vline" && kind != "label" &&
          kind != "console" && kind != "station" && kind != "controls")
        throw DescriptorError(at + ": kind must be section, hline, vline, label, console, station or controls");
      if (!e.contains("at")) throw DescriptorError(at + ": needs \"at\": [column, row]");
      check_at(e["at"], at);
      if (e["at"][0].get<int>() >= columns)
        throw DescriptorError(at + ": at is in column " + std::to_string(e["at"][0].get<int>()) + ", past the last of " +
                              std::to_string(columns));
      json item = {{"kind", kind}, {"at", e["at"]}};
      const bool line = kind == "hline" || kind == "vline";
      if (e.contains("span")) {
        const auto& span = e["span"];
        if (line) {
          if (!span.is_number_integer() || span < 1) throw DescriptorError(at + ": a separator's span is a whole number of cells, from 1");
        } else if (!span.is_array() || span.size() != 2 || !span[0].is_number_integer() || !span[1].is_number_integer() || span[0] < 1 ||
                   span[1] < 1) {
          throw DescriptorError(at + ": a " + kind + "'s span is [columns, rows], whole numbers from 1");
        }
        const int c = e["at"][0].get<int>();
        const int w = line ? (kind == "hline" ? span.get<int>() : 1) : span[0].get<int>();
        if (c + w > columns)
          throw DescriptorError(at + ": spans past the last column (" + std::to_string(columns) + "): at column " + std::to_string(c) + " it can take " +
                                std::to_string(columns - c));
        item["span"] = span;
      } else if (line) {
        throw DescriptorError(at + ": a separator needs a span: how many cells it runs");
      }
      if (e.contains("text")) {
        if (line || kind == "console" || kind == "station" || kind == "controls" || !e["text"].is_string())
          throw DescriptorError(at + (line ? ": a separator has no text" : kind == "console" ? ": the console has no text" : kind == "station" ? ": the station has no text" : kind == "controls" ? ": the controls tile has no text" : ": text must be text"));
        item["text"] = e["text"];
      } else if (kind == "label") {
        throw DescriptorError(at + ": a label needs its text");
      }
      if (e.contains("style")) {
        const std::string style = e["style"].is_string() ? e["style"].get<std::string>() : "";
        if (kind != "label") throw DescriptorError(at + ": only a label has a style");
        if (style != "heading" && style != "note") throw DescriptorError(at + ": style must be heading or note");
        item["style"] = style;
      }
      check_tab(e, at);
      if (e.contains("tab")) item["tab"] = e["tab"];
      decorations.push_back(std::move(item));
    }
    out["decorations"] = std::move(decorations);
  }
  catalog.dashboard = out.dump();
}

// [[variables]]: the backend's default variables (#36), in the protocol's shape. A type may
// be left out when the value says it (a number, true / false, text; a formula is a number).
void load_variables(const json& doc, Catalog& catalog) {
  if (!doc.contains("variables")) return;
  const json& list = doc.at("variables");
  if (!list.is_array()) throw DescriptorError("variables: a list of [[variables]] tables");
  json out = json::array();
  for (std::size_t i = 0; i < list.size(); ++i) {
    json v = list[i];
    const auto at = "variables[" + std::to_string(i) + "]";
    only_fields(v, {"name", "type", "value", "expr", "unit", "min", "max", "step", "whole", "options", "note", "group", "exposed", "dock", "tunable"}, at);
    if (!v.contains("type")) {
      const json& value = v.value("value", json());
      v["type"] = v.contains("expr") || value.is_number() ? "number" : value.is_boolean() ? "boolean" : "text";
    }
    out.push_back(std::move(v));
  }
  try {
    const Variables check(out);
  } catch (const VariableError& err) {
    throw DescriptorError(std::string("variables: ") + err.what());
  }
  catalog.variables = out.dump();
}

// [graph] and [station]: the starters, in the protocol's shapes, checked against the
// catalog so a typo fails at load, not when the UI opens.
void load_starters(const json& doc, Catalog& catalog) {
  json graph = doc.value("graph", json::object());
  only_fields(graph, {"nodes", "edges", "groups"}, "graph");
  const json groups = doc.value("graph", json::object()).value("groups", json::array());
  graph = {{"nodes", graph.value("nodes", json::array())}, {"edges", graph.value("edges", json::array())}};
  std::set<std::string> ids;
  for (std::size_t i = 0; i < graph["nodes"].size(); ++i) {
    auto& n = graph["nodes"][i];
    const auto at = "graph.nodes[" + std::to_string(i) + "]";
    only_fields(n, {"id", "type", "params", "pos", "ports"}, at);
    if (n.contains("ports")) only_fields(n["ports"], {"left", "right", "top", "bottom"}, at + ".ports");
    if (!n.contains("id") || !n.contains("type")) throw DescriptorError(at + ": needs an id and a type");
    const auto type = n["type"].get<std::string>();
    if (!catalog.find(type)) {
      if (catalog.missing_imports.empty()) {
        throw DescriptorError(at + ": no block type \"" + type + "\" in this catalog");
      }
    }
    if (!ids.insert(n["id"].get<std::string>()).second) throw DescriptorError(at + ": duplicate id");
    if (!n.contains("params")) n["params"] = json::object();
  }
  // Groups: a visual frame, or a story (one step of a Step flow). Its id is also a node id
  // for step wires (its ports: in, next).
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto& g = groups[i];
    const auto at = "graph.groups[" + std::to_string(i) + "]";
    only_fields(g, {"id", "kind", "label", "nodes", "parent", "collapsed", "pos", "params", "ports", "release", "expose"}, at);
    if (g.contains("ports")) only_fields(g["ports"], {"left", "right", "top", "bottom"}, at + ".ports");
    if (!g.contains("id") || !g.contains("nodes")) throw DescriptorError(at + ": needs an id and nodes");
    const auto kind = g.value("kind", std::string{"visual"});
    if (kind != "visual" && kind != "subsystem" && kind != "story") throw DescriptorError(at + ": kind must be subsystem, story or visual");
    if (!ids.insert(g["id"].get<std::string>()).second) throw DescriptorError(at + ": duplicate id");
    for (const auto& n : g["nodes"])
      if (!ids.contains(n.get<std::string>())) throw DescriptorError(at + ": no node \"" + n.get<std::string>() + "\"");
    // Ports the group shows as a block even with no wire: { side: "in" | "out", to: [node, port] }.
    for (const auto& x : g.value("expose", json::array())) {
      if (kind == "visual") throw DescriptorError(at + ": only a subsystem or story exposes ports");
      only_fields(x, {"side", "to"}, at + ".expose");
      const auto side = x.value("side", std::string{});
      if (side != "in" && side != "out") throw DescriptorError(at + ".expose: side must be in or out");
      if (!x.contains("to") || !x["to"].is_array() || x["to"].size() != 2 || !x["to"][0].is_string() || !x["to"][1].is_string())
        throw DescriptorError(at + ".expose: to is [node, port]");
      if (!ids.contains(x["to"][0].get<std::string>()))
        throw DescriptorError(at + ".expose: no node \"" + x["to"][0].get<std::string>() + "\"");
    }
    // A story's outputs that drop when it ends: [node, port], each a block of the story.
    for (const auto& r : g.value("release", json::array())) {
      if (kind != "story") throw DescriptorError(at + ": only a story releases outputs");
      if (!r.is_array() || r.size() != 2 || !r[0].is_string() || !r[1].is_string())
        throw DescriptorError(at + ": release lists outputs as [node, port]");
      if (!std::ranges::any_of(g["nodes"], [&](const json& n) { return n == r[0]; }))
        throw DescriptorError(at + ": releases " + r[0].get<std::string>() + "." + r[1].get<std::string>() +
                              ", which isn't in the story");
    }
  }
  // Nesting: a group's parent is another group, and no group ends up inside itself.
  std::map<std::string, std::string> parent_of;
  for (const auto& g : groups)
    if (g.contains("parent")) parent_of[g["id"].get<std::string>()] = g["parent"].get<std::string>();
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto at = "graph.groups[" + std::to_string(i) + "]";
    auto id = groups[i]["id"].get<std::string>();
    for (std::size_t depth = 0; parent_of.contains(id); ++depth) {
      const auto& up = parent_of.at(id);
      if (!std::ranges::any_of(groups, [&](const json& g) { return g["id"] == up; }))
        throw DescriptorError(at + ": no group \"" + up + "\" for its parent");
      if (depth >= groups.size()) throw DescriptorError(at + ": is inside itself (its parents go round)");
      id = up;
    }
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
    if (!catalog.find_object(o["type"].get<std::string>())) {
      if (catalog.missing_imports.empty()) {
        throw DescriptorError(at + ": no station object type \"" + o["type"].get<std::string>() + "\"");
      }
    }
    if (!o.contains("props")) o["props"] = json::object();
  }
  std::set<std::string> object_ids;
  for (const auto& o : station["objects"]) object_ids.insert(o["id"].get<std::string>());
  for (const auto& o : station["objects"]) {
    const ObjectEntry* entry = catalog.find_object(o["type"].get<std::string>());
    if (!entry) continue;
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

Catalog load_descriptor(std::string_view toml_text, const Registry& registry, const DescriptorOptions& options) {
  json doc;
  try {
    doc = to_json(toml::parse(toml_text));
  } catch (const toml::parse_error& err) {
    throw DescriptorError("not valid TOML: " + at_line(err.source()) + ": " + std::string(err.description()));
  }
  only_fields(doc, {"domain", "import", "toolkit", "toolkits", "blocks", "objects", "graph", "station", "dashboard", "variables", "connectors"}, "descriptor");

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
  if (!catalog.domain.empty() &&
      (!registry.package(catalog.domain).empty() || !registry.objects(catalog.domain).empty() ||
       !registry.connectors(catalog.domain).empty()))
    imports.push_back(catalog.domain);

  auto add_import_name = [&](const std::string& name) {
    if (name == "__all__") {
      // Add all packages from registry (all built-in packages) and optional toolkits
      for (const auto& pkg : registry.packages()) {
        if (pkg != catalog.domain && std::ranges::find(imports, pkg) == imports.end()) {
          imports.push_back(pkg);
        }
      }
      for (const auto& opt : options.optional_imports) {
        if (opt != "__all__" && opt != catalog.domain && std::ranges::find(imports, opt) == imports.end()) {
          imports.push_back(opt);
        }
      }
    } else if (name != catalog.domain && std::ranges::find(imports, name) == imports.end()) {
      imports.push_back(name);
    }
  };

  for (const char* key : {"import", "toolkit", "toolkits"}) {
    if (!doc.contains(key)) continue;
    if (doc[key].is_array()) {
      for (const auto& item : doc[key]) {
        if (item.is_string()) add_import_name(item.get<std::string>());
      }
    } else if (doc[key].is_string()) {
      add_import_name(doc[key].get<std::string>());
    }
  }
  for (const auto& package : imports) {
    const auto templates = registry.package(package);
    const auto station = registry.objects(package);
    const auto conns = registry.connectors(package);
    if (templates.empty() && station.empty() && conns.empty()) {
      if (std::ranges::find(options.optional_imports, package) != options.optional_imports.end()) {
        catalog.missing_imports.push_back(package);
        continue;
      }
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
    for (const ConnectorEntry* c : conns) {
      if (!catalog.connectors.is_object()) catalog.connectors = nlohmann::json::object();
      catalog.connectors[c->name] = c->def;
    }
  }
  if (doc.contains("connectors")) {
    const json& ctable = doc.at("connectors");
    if (!ctable.is_object()) throw DescriptorError("connectors must be a table");
    if (!catalog.connectors.is_object()) catalog.connectors = nlohmann::json::object();
    for (const auto& [name, def] : ctable.items()) {
      if (!def.is_object()) throw DescriptorError("connectors." + name + " must be a table");
      catalog.connectors[name] = nlohmann::json::parse(def.dump());
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
  load_variables(doc, catalog);
  load_starters(doc, catalog);
  // The starters' references (#36) resolve against the defaults, or the descriptor is wrong.
  try {
    const Variables vars(catalog.variables.empty() ? json() : json::parse(catalog.variables));
    resolve_graph(json::parse(catalog.graph), catalog, vars);
    if (!catalog.station.empty()) resolve_station(json::parse(catalog.station), catalog, vars);
  } catch (const VariableError& err) {
    throw DescriptorError(std::string("starter: ") + err.what());
  }
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

Catalog load_descriptor_file(const std::filesystem::path& path, const Registry& registry, const DescriptorOptions& options) {
  std::ifstream in(path);
  if (!in) throw DescriptorError("cannot open " + path.string());
  std::stringstream text;
  text << in.rdbuf();
  Catalog catalog = load_descriptor(text.str(), registry, options);
  catalog.dir = std::filesystem::absolute(path).parent_path();
  if (!catalog.station_page.empty() && !std::filesystem::is_regular_file(catalog.dir / catalog.station_page))
    throw DescriptorError("station.page: no file " + (catalog.dir / catalog.station_page).string());
  return catalog;
}

std::string catalog_message(const Catalog& catalog, int indent) {
  auto ports = [](const std::vector<PortDef>& list) {
    json out = json::array();
    for (const auto& p : list) {
      json port = {{"id", p.id}, {"type", p.type}};
      if (!p.unit.empty()) port["unit"] = p.unit;
      if (p.dimension) port["dimension"] = *p.dimension;
      if (p.broadcast) port["broadcast"] = true;
      if (!p.components.empty()) {
        json components = json::array();
        for (const auto& component : p.components) {
          json item = {{"id", component.id}};
          if (!component.unit.empty()) item["unit"] = component.unit;
          components.push_back(std::move(item));
        }
        port["components"] = std::move(components);
      }
      out.push_back(std::move(port));
    }
    return out;
  };
  auto param_list = [](const std::vector<ParamDef>& list) {
    json out = json::array();
    for (const auto& p : list) {
      if (p.type == "number") out.push_back({{"id", p.id}, {"type", "number"}, {"default", p.default_value}});
      else if (p.type == "boolean") out.push_back({{"id", p.id}, {"type", "boolean"}, {"default", p.default_value != 0}});
      else if (p.type == "enum") out.push_back({{"id", p.id}, {"type", "enum"}, {"default", p.default_text}, {"options", p.options}});
      else out.push_back({{"id", p.id}, {"type", p.type}, {"default", p.default_text}});
      if (!p.unit.empty()) out.back()["unit"] = p.unit;
    }
    return out;
  };
  json blocks = json::array();
  for (const auto& e : catalog.entries) {
    json params = param_list(e.def.params);
    const BlockDef shown = with_broadcast(e.def);  // an elementwise block's signal ports take vectors
    json block = {{"type", e.def.type},
                  {"label", e.def.label},
                  {"kind", to_string(e.def.kind)},
                  {"inputs", ports(shown.inputs)},
                  {"outputs", ports(shown.outputs)},
                  {"params", params}};
    if (!e.group.empty()) block["group"] = e.group;
    if (e.linked) block["linked"] = true;
    if (!e.def.instrument.empty()) block["instrument"] = e.def.instrument;
    if (!e.def.description.empty()) block["description"] = e.def.description;
    if (!e.def.feedthrough) block["feedthrough"] = false;
    if (!e.def.ports_from.empty()) block["ports_from"] = e.def.ports_from;
    if (!e.def.inputs_from.empty()) block["inputs_from"] = e.def.inputs_from;
    if (!e.def.outputs_from.empty()) block["outputs_from"] = e.def.outputs_from;
    if (e.def.elementwise) block["elementwise"] = true;
    if (!e.def.view.empty()) {
      json view = json::object();
      if (e.def.view.compact) view["compact"] = *e.def.view.compact;
      if (!e.def.view.glyph.empty()) view["glyph"] = e.def.view.glyph;
      if (!e.def.view.value.empty()) view["value"] = e.def.view.value;
      if (!e.def.view.icon.is_null()) view["icon"] = json::parse(e.def.view.icon.dump());
      block["view"] = std::move(view);
    }
    if (!e.def.forms.empty()) {
      json forms = json::array();
      for (const auto& f : e.def.forms)
        forms.push_back({{"name", f.name}, {"inputs", ports(f.inputs)}, {"outputs", ports(f.outputs)}});
      block["forms"] = std::move(forms);
    }
    blocks.push_back(std::move(block));
  }
  json msg = {{"type", "catalog"}, {"blocks", blocks}};
  if (!catalog.missing_imports.empty()) msg["missing_imports"] = catalog.missing_imports;
  if (catalog.connectors.is_object() && !catalog.connectors.empty())
    msg["connectors"] = json::parse(catalog.connectors.dump());
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
