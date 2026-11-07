#pragma once

// Modelica `.mo` / MSL icon and connector importer (#160 contracts §3, §4, §6, §7).
// Resolves inherited connectors (`extends`), quantises `Placement(iconTransformation)` onto
// 5-unit edge slots with collision spreading, and quantises `Icon(graphics)` onto Paperboat's
// design-token primitive set (`view.icon`).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/modelica/omc.hpp"

namespace paperboat::modelica {

struct MovedPort {
  std::string id = {};
  int distance = 0;  // 0..100 units shifted along its edge during collision spreading
};

struct ImportNotice {
  int simplified = 0;                  // flattened gradients/patterns, Beziers, fallback connectors…
  std::vector<std::string> dropped{};  // unsupported items dropped, e.g. "Bitmap", "DynamicSelect"
  std::vector<MovedPort> moved{};      // ports moved to resolve edge-slot collisions
};

struct VarUnit {
  std::string id = {};
  std::string unit = {};
};

struct ConnectorDef {
  std::string kind = "acausal";   // "acausal" | "input" | "output" | "expandable"
  std::string handle = "square";  // "square" | "diamond" | "ring2" | "triangle" | "circle"
  std::string alt = {};           // "" | "hollow" | "filled"
  int size = 8;                   // 8 | 10 | 12
  double weight = 1.2;            // 1.2 | 1.8 | 2.0
  std::string dash = {};          // "" | "solid" | "dashed"
  std::vector<VarUnit> potential{};
  std::vector<VarUnit> flow{};
  std::vector<VarUnit> stream{};
};

struct IconPort {
  std::string id = {};
  std::string class_name = {};  // short Modelica connector class name (key in `connectors`)
  std::string side = "left";    // "left" | "right" | "top" | "bottom"
  int at = 50;                  // 0..100 along the edge, multiple of 5
  std::string variant = {};     // "" | "hollow" | "filled"
  std::string array = {};       // param id holding n (#164 vector port), or ""
  std::string condition = {};   // param/expr from `if <cond>`, serialised as `"if"`
};

struct IconExtent {
  double x1 = -100.0;
  double y1 = -100.0;
  double x2 = 100.0;
  double y2 = 100.0;
};

// Unquantised port extracted from Modelica components + Placement annotations.
struct RawPort {
  std::string id = {};
  std::string class_name = {};  // short connector class name
  std::string full_class = {};  // fully-qualified connector class name
  std::string kind = "acausal";
  std::string variant = {};
  std::string array = {};
  std::string condition = {};
  bool has_placement = false;
  double origin_x = 0.0;
  double origin_y = 0.0;
  double x1 = -10.0;
  double y1 = -10.0;
  double x2 = 10.0;
  double y2 = 10.0;
  double rotation = 0.0;  // degrees anticlockwise
};

struct ImportedModel {
  std::string model = {};                            // resolved Modelica class name
  nlohmann::json icon = nlohmann::json::object();    // IconDef JSON: {w?, h?, g: [...], ports?: [...]}
  std::map<std::string, ConnectorDef> connectors{};  // keyed by short connector class name
  ImportNotice notice{};
  IconExtent extent{};               // raw Icon coordinateSystem extent (for generator re-quantisation)
  std::vector<RawPort> raw_ports{};  // raw resolved connector ports before edge-slot quantisation

  nlohmann::json to_json() const;  // {icon, connectors, notice}
};

nlohmann::json to_json(const ConnectorDef& def);
nlohmann::json to_json(const IconPort& port);
nlohmann::json to_json(const ImportNotice& notice);

// Parses a Modelica call atom `Name(a, b, ...)` using `parse_value` on its argument tuple.
std::optional<std::pair<std::string, OmValue>> parse_call(std::string_view atom);

// Extracts `Placement(iconTransformation / transformation)` from one component's annotation list.
bool parse_placement(const OmValue& annotations, RawPort& port);

// Quantises raw ports onto `{side, at}` (0..100, multiples of 5, rotation snapped to 90°,
// mirroring kept) and spreads collisions evenly across free slots on that side.
std::vector<IconPort> quantise_ports(const std::vector<RawPort>& raw, ImportNotice& notice,
                                     IconExtent extent = {});

// Quantises a sequence of `getIconAnnotation` values (base-to-derived order) plus `ports`
// into a token-only `IconDef` JSON object (`view.icon`).
nlohmann::json quantise_icon(const std::vector<OmValue>& icon_annotations, const std::vector<IconPort>& ports,
                             ImportNotice& notice, IconExtent* out_extent = nullptr);

// Walks `class_name` and its `extends` bases in `omc` to collect connector instances and
// resolve each connector class's variables, units and Contract §4 handle definition.
void resolve_connectors(Omc& omc, std::string_view class_name, std::vector<RawPort>& out_ports,
                        std::map<std::string, ConnectorDef>& out_connectors, ImportNotice& notice);

// The inverse of quantise_icon (Studio's Look tab, squero/0 `set_icon`): an IconDef's `w`, `h`
// and `g` as a Modelica `Icon(coordinateSystem(...), graphics={...})` annotation. Tokens map
// to fixed colours, thicknesses and patterns, so importing the text again gives `g` back.
// `ports` are not written: connector placement belongs to the components, not the icon.
// Throws OmcError on an unknown primitive or token.
std::string icon_annotation(const nlohmann::json& icon);

// Imports a loaded or MSL Modelica class (`Modelica.Electrical.Analog.Basic.Resistor`).
ImportedModel import_class(Omc& omc, std::string_view class_name);

// Imports either a `.mo` file (`source` is an existing file; optional `class_name` selects
// among classes in the file) or a qualified class name (`source` empty, `class_name` set).
ImportedModel import_model(Omc& omc, const std::filesystem::path& source, std::string_view class_name = {});

}  // namespace paperboat::modelica
