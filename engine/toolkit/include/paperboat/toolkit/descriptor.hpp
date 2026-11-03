#pragma once

// Domain descriptors: a plain-text (TOML) file that says what a domain's app offers. It
// picks blocks and station objects from templates (toolkit packages, and the domain's own
// package, registered in code under the domain's name and imported without being listed),
// optionally renamed and re-defaulted; `import` adds whole packages. It also holds the
// starters: the graph and station the UI opens with, and how the Station page shows the
// station (`[station] view`). A descriptor never defines a block: code does (GUIDELINES §6).
// See toolkit/README.md for the format.

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/block.hpp"
#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit {

struct CatalogEntry {
  BlockDef def;                      // after the descriptor's overrides
  const Template* from = nullptr;    // the template; null only for a linked block
  Config config;                     // template config with the descriptor's values applied
  std::string group;                 // palette group: the template's package, or the domain
  bool linked = false;               // spawned by a station object: not in the palette, no params

  // Make an instance. Params missing from `params` take def's defaults. Throws
  // std::logic_error for a linked block (its station object acts for it) and for the wrong
  // one of the two (make() on a step template, make_step() on a signal one).
  // `text` holds text params ("key" params) by id; missing ones take their defaults.
  std::unique_ptr<Block> make(const Values& params = {}, const Config& text = {}) const;
  std::unique_ptr<Step> make_step(const Values& params = {}, const Config& text = {}) const;
};

// A station object type in a catalog. Its linked block (if any) is also in `entries`,
// marked `linked`, under the block's type.
struct ObjectEntry {
  ObjectDef def;                           // after the descriptor's overrides
  const StationTemplate* from = nullptr;   // the station template
  std::string group;

  // Make an instance. Props missing from `props` take def's defaults.
  std::unique_ptr<StationObject> make(const Values& props = {}) const;
};

struct Catalog {
  std::string domain;
  std::filesystem::path dir;  // the descriptor's folder: "file" props are relative to it
  std::vector<CatalogEntry> entries;
  std::vector<ObjectEntry> objects;
  // Starters from the descriptor's optional [graph] and [station] tables, as the
  // protocol's JSON text: graph.load's graph ({"nodes":[],"edges":[]} when absent) and
  // station.load's station ("" when absent). Checked against this catalog at load.
  std::string graph;
  std::string station;
  // How the Station page shows the station: "scene" (the built-in 2D/3D scene), "graph"
  // (station objects as nodes, their links as wires) or "web" (the backend's own page,
  // `station_page`, relative to `dir`, served by the engine).
  std::string station_view = "scene";
  std::string station_page;

  const CatalogEntry* find(std::string_view type) const;
  const ObjectEntry* find_object(std::string_view type) const;
};

struct DescriptorError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

Catalog load_descriptor(std::string_view toml_text, const Registry& registry = builtins());
Catalog load_descriptor_file(const std::filesystem::path& path, const Registry& registry = builtins());

// Blocks defined in TOML on parents in `registry` (C++ templates or earlier definitions),
// added to it as "<package>/<type>": `[[blocks]]` entries with `use` (the parent), `type`,
// and optionally `label`, `params` (new ones, if the parent is extendable), `defaults` and
// `config`. How a package ships blocks that are only a definition (signal/sine on
// signal/generator).
void define_blocks(Registry& registry, const std::string& package, std::string_view toml_text);

// The `domain` name in a descriptor's text, before loading it (a host that registers the
// domain's own templates needs the package name first).
std::string descriptor_domain(std::string_view toml_text);

// The protocol's `catalog` message for this catalog, as JSON text (compact when indent < 0).
std::string catalog_message(const Catalog& catalog, int indent = -1);

}  // namespace paperboat::toolkit
