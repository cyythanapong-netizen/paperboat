#pragma once

// Templates: reusable blocks that a domain descriptor refers to by name, such as
// "math/gain". A template is a BlockDef plus a factory for instances.

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "paperboat/toolkit/block.hpp"
#include "paperboat/toolkit/station.hpp"

namespace paperboat::toolkit {

struct Template {
  std::string name;  // "<package>/<block>", e.g. "math/gain"
  BlockDef def;      // def.type is the default type id in a catalog ("pid")
  // Descriptor-time settings this template accepts, with their defaults (e.g. key → "Space").
  Config config;
  // A parent for definitions in TOML: `extendable` lets a definition that `use`s it add
  // params of its own (and set any config); `abstract` keeps it out of `import` (it is
  // only a parent); `check` validates a definition on it at load (an expression that
  // doesn't compile, say), throwing std::exception with what's wrong.
  bool extendable = false;
  bool abstract = false;
  std::function<void(const BlockDef& def, const Config& config)> check;
  // Make an instance. `params` holds every number param in def.params; `config` every key
  // in `config` plus every text param (a "key" param) by its id.
  // A template sets one of the two: `make` for a signal block, `make_step` for a block on
  // a Step flow's path (one with step ports).
  std::function<std::unique_ptr<Block>(const Values& params, const Config& config)> make;
  std::function<std::unique_ptr<Step>(const Values& params, const Config& config)> make_step;
};

// A station template: a reusable station object type, such as "opencv/image".
struct StationTemplate {
  std::string name;  // "<package>/<object>"
  ObjectDef def;
  // Make an instance. `props` holds every prop in def.props.
  std::function<std::unique_ptr<StationObject>(const Values& props)> make;
};

// A Modelica connector class definition registered by a package or domain (§4).
struct ConnectorEntry {
  std::string package = {};
  std::string name = {};         // Modelica class short name, e.g. "Pin" or "RealInput"
  nlohmann::json def = nullptr;  // ConnectorDef JSON object
};

class Registry {
 public:
  void add(Template t);  // throws std::invalid_argument on a duplicate name
  void add(StationTemplate t);
  void add_connector(std::string package, std::string name, nlohmann::json def);
  const Template* find(std::string_view name) const;
  const StationTemplate* find_object(std::string_view name) const;
  // The station templates of one package, in the order they were added.
  std::vector<const StationTemplate*> objects(std::string_view package) const;
  // The connector definitions of one package, in the order they were added.
  std::vector<const ConnectorEntry*> connectors(std::string_view package) const;
  std::vector<const Template*> all() const;  // sorted by name
  // The templates of one package ("base"), in the order they were added.
  std::vector<const Template*> package(std::string_view name) const;
  std::vector<std::string> packages() const;  // names, sorted
  // A package's flow types (#9): every template of `package` that names none of its own
  // gets `flows` (its toolkit.toml's `flows`). Throws std::invalid_argument on an unknown one.
  void default_flows(std::string_view package, const std::vector<std::string>& flows);

 private:
  std::vector<Template> templates_;
  std::vector<StationTemplate> objects_;
  std::vector<ConnectorEntry> connectors_;
};

// Defining a block or station object in code: a domain's own, or a package's. `B` derives
// from Block or Step and is constructed from (params, config), (params) or nothing; `O`
// derives from StationObject and is constructed from (props) or nothing. The template is
// named "<package>/<def.type>"; a domain registers its own under its domain name, and its
// descriptor gets them without listing them.
//
//   registry.add(define_block<Count>("parts", {.type = "count", .label = "Count parts",
//                                              .inputs = {{"mask", "image"}}, .outputs = {{"total"}}}));
template <class T, class Base>
std::unique_ptr<Base> construct(const Values& params, const Config& config) {
  if constexpr (std::is_constructible_v<T, const Values&, const Config&>) return std::make_unique<T>(params, config);
  else if constexpr (std::is_constructible_v<T, const Values&>) return std::make_unique<T>(params);
  else return std::make_unique<T>();
}

template <class B>
Template define_block(const std::string& package, BlockDef def, Config config = {}) {
  static_assert(std::is_base_of_v<Block, B> || std::is_base_of_v<Step, B>, "B must derive from Block or Step");
  Template t;
  t.name = package + "/" + def.type;
  t.def = std::move(def);
  t.config = std::move(config);
  if constexpr (std::is_base_of_v<Step, B>) {
    t.make_step = &construct<B, Step>;
    if (t.def.flows.empty()) t.def.flows = {"step"};  // a step on a path: a Step flow's, unless it says
  } else {
    t.make = &construct<B, Block>;
  }
  return t;
}

template <class O>
StationTemplate define_object(const std::string& package, ObjectDef def) {
  static_assert(std::is_base_of_v<StationObject, O>, "O must derive from StationObject");
  StationTemplate t;
  t.name = package + "/" + def.type;
  t.def = std::move(def);
  t.make = [](const Values& props) { return construct<O, StationObject>(props, {}); };
  return t;
}

// Every built-in template.
const Registry& builtins();

}  // namespace paperboat::toolkit
