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
#include <optional>
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
  // The optional [dashboard] table: the default layout of the Dashboard's grid, as the
  // protocol's JSON text ("" when absent). Sent in the catalog message.
  std::string dashboard;
  // The optional top-level [connectors] table (merged with C++-registered connectors):
  // Modelica connector class definitions (§4), sent in the catalog message when non-empty.
  nlohmann::json connectors = nullptr;
  // The optional [[variables]]: the backend's default variables (#36), as the protocol's
  // JSON text (a list; "" when absent). Sent in variables.load; the starters may use them.
  std::string variables;
  // How the Station page shows the station: "scene" (the built-in 2D/3D scene), "graph"
  // (station objects as nodes, their links as wires) or "web" (the backend's own page,
  // `station_page`, relative to `dir`, served by the engine).
  std::string station_view = "scene";
  std::string station_page;
  // Whether the Station page is offered (the descriptor's [station] visible, default true).
  // False hides it in the UI: the backend's default, which the user can change in View.
  bool station_visible = true;
  std::vector<std::string> missing_imports;
  // The flow types the domain offers (#9): what a sheet may be, sent in hello. The
  // descriptor's top-level `flows`, else a Signal flow and every flow type a block of the
  // catalog names (flow_types() order).
  std::vector<std::string> flows;

  const CatalogEntry* find(std::string_view type) const;
  const ObjectEntry* find_object(std::string_view type) const;
};

struct DescriptorOptions {
  std::vector<std::string> optional_imports;
};

struct DescriptorError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

Catalog load_descriptor(std::string_view toml_text, const Registry& registry = builtins(), const DescriptorOptions& options = {});
Catalog load_descriptor_file(const std::filesystem::path& path, const Registry& registry = builtins(), const DescriptorOptions& options = {});

// Blocks defined in TOML on parents in `registry` (C++ templates or earlier definitions),
// added to it as "<package>/<type>": `[[blocks]]` entries with `use` (the parent), `type`,
// and optionally `label`, `params` (new ones, if the parent is extendable), `defaults` and
// `config`. How a package ships blocks that are only a definition (signal/sine on
// signal/generator).
void define_blocks(Registry& registry, const std::string& package, std::string_view toml_text);

// The `domain` name in a descriptor's text, before loading it (a host that registers the
// domain's own templates needs the package name first).
std::string descriptor_domain(std::string_view toml_text);

// A sheet's flow type (#9) against its blocks: the graph's `flow` ("signal", "step"…) must be
// one the catalog offers, and each block, one that works in its sheet: the graph's type, or a
// Signal flow inside a story (a story runs its blocks as a Signal flow). A graph without `flow`
// is not checked (it was made before sheets had a type); nor are commented-out blocks, or
// types the catalog lacks (the graph's own check reports those). `node`: the block, if any.
struct FlowMismatch {
  std::string node;
  std::string message;
};
std::optional<FlowMismatch> flow_mismatch(const nlohmann::json& graph, const Catalog& catalog);

// The protocol's `catalog` message for this catalog, as JSON text (compact when indent < 0).
std::string catalog_message(const Catalog& catalog, int indent = -1);

}  // namespace paperboat::toolkit
