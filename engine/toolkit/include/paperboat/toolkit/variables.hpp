#pragma once

// Variables (#36): named, typed values defined once and used by reference in block params
// and station props, so one `pole_length` feeds a station object and a controller gain.
//
//   a param or prop's value:  1.5                         a literal, as always
//                             {"var": "pole_length"}      a variable's value
//                             {"expr": "2 * sqrt(kp)"}    a formula over variables (numbers)
//
// Resolved before blocks and objects are made: a block never sees a variable, only the
// number or text it stands for. Types follow ParamDef's: number (unit, min, max, whole),
// boolean, enum (options), text and file. A number or boolean variable may itself be a
// formula over other variables ("expr"); formulas are evaluated in dependency order.

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/toolkit/block.hpp"

namespace paperboat::toolkit {

struct Catalog;

// A variable list or a reference that doesn't hold. `where` names what is at fault: a
// variable, or the block or station object whose param refers to one.
struct VariableError : std::runtime_error {
  VariableError(std::string at, const std::string& what) : std::runtime_error(what), where(std::move(at)) {}
  std::string where;
};

class Variables {
 public:
  Variables() = default;
  // The protocol's list ([{name, type, value | expr, unit, min, max, whole, options, …}]),
  // checked and resolved. Throws VariableError naming the variable at fault.
  explicit Variables(const nlohmann::json& list);

  bool empty() const { return vars_.empty(); }
  // name → resolved value (a number, true / false, or text).
  nlohmann::json values() const;

  // A param's (or prop's) value with any reference resolved and checked against `def`: a
  // literal as it is, {"var"} its variable's value, {"expr"} the formula's value (number
  // and boolean params only). `where` names the block or object for errors
  // ("pid-1.kp: unknown variable \"kp\" (kp_x?)").
  nlohmann::json value_for(const nlohmann::json& given, const ParamDef& def, std::string_view where) const;

 private:
  struct Var {
    std::string type;          // number, boolean, enum, text, file
    nlohmann::json value;      // resolved
    std::vector<std::string> options;
  };
  double evaluate(const std::string& text, std::string_view where) const;
  std::map<std::string, Var, std::less<>> vars_;
};

// A graph (graph.load's shape) or station (station.load's) with every reference in its
// params / props resolved, checked against the catalog's definitions. A node or object of
// an unknown type is left as it is (making it reports that). Throws VariableError naming
// the block or object.
nlohmann::json resolve_graph(const nlohmann::json& graph, const Catalog& catalog, const Variables& vars);
nlohmann::json resolve_station(const nlohmann::json& station, const Catalog& catalog, const Variables& vars);

// True for {"var": …} or {"expr": …}.
bool is_reference(const nlohmann::json& value);

// A name a variable may have: [A-Za-z_][A-Za-z0-9_]*, not a formula's own (t, pi, e, a
// function's name).
bool valid_variable_name(std::string_view name);

}  // namespace paperboat::toolkit
