#include "paperboat/toolkit/variables.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/expression.hpp"

namespace paperboat::toolkit {

using json = nlohmann::json;

namespace {

const std::set<std::string, std::less<>> kTypes = {"number", "boolean", "enum", "text", "file"};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The names a formula uses, other than its built-ins (pi, sin…), in order of appearance.
std::vector<std::string> names_in(std::string_view text) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < text.size();) {
    if (ident_start(text[i])) {
      std::size_t j = i;
      while (j < text.size() && ident_char(text[j])) ++j;
      std::string name(text.substr(i, j - i));
      if (!Expression::is_builtin(name) && std::ranges::find(out, name) == out.end()) out.push_back(name);
      i = j;
    } else if (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.') {  // a number, 1.5e-3 included
      std::size_t j = i;
      while (j < text.size() && (std::isdigit(static_cast<unsigned char>(text[j])) || text[j] == '.')) ++j;
      if (j < text.size() && (text[j] == 'e' || text[j] == 'E')) {
        ++j;
        if (j < text.size() && (text[j] == '+' || text[j] == '-')) ++j;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
      }
      i = j;
    } else {
      ++i;
    }
  }
  return out;
}

std::size_t distance(std::string_view a, std::string_view b) {
  std::vector<std::size_t> row(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    std::size_t diag = row[0];
    row[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t up = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diag = up;
    }
  }
  return row[b.size()];
}

std::string number_text(double x) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", x);
  return buf;
}

std::string kind_of(const json& v) {
  if (v.is_boolean()) return "true / false";
  if (v.is_number()) return "a number";
  if (v.is_string()) return "text";
  return "not a value";
}

// The closest of `names` to `name` (at most two edits away; a shared first letter breaks a
// tie), or "" when none is close: the "(kp_x?)" of an unknown name.
template <class Map>
std::string nearest(std::string_view name, const Map& names) {
  std::string best;
  std::size_t best_d = 3;
  bool best_first = false;
  for (const auto& [other, _] : names) {
    const auto d = distance(name, other);
    const bool first = !name.empty() && !other.empty() && name[0] == other[0];
    if (d < best_d || (d == best_d && first && !best_first)) best = other, best_d = d, best_first = first;
  }
  return best;
}

}  // namespace

bool is_reference(const json& value) { return value.is_object() && (value.contains("var") || value.contains("expr")); }

bool valid_variable_name(std::string_view name) {
  if (name.empty() || !ident_start(name[0])) return false;
  if (!std::ranges::all_of(name, ident_char)) return false;
  return name != "t" && !Expression::is_builtin(name);
}

Variables::Variables(const json& list) {
  if (list.is_null()) return;
  if (!list.is_array()) throw VariableError("", "variables must be a list");
  std::map<std::string, std::string, std::less<>> formulas;  // name → its formula
  std::map<std::string, json, std::less<>> defs;
  std::vector<std::string> order;
  for (const auto& v : list) {
    if (!v.is_object() || !v.contains("name") || !v["name"].is_string())
      throw VariableError("", "a variable needs a name");
    const auto name = v["name"].get<std::string>();
    if (!valid_variable_name(name))
      throw VariableError(name, "\"" + name + "\" can't be a variable's name: use letters, digits and _, not starting with a digit, "
                                "and not t, pi, e or a function's name");
    if (defs.contains(name)) throw VariableError(name, "two variables are called " + name);
    const auto type = v.value("type", std::string{});
    if (!kTypes.contains(type)) throw VariableError(name, name + ": type must be number, boolean, enum, text or file");
    const bool has_value = v.contains("value"), has_expr = v.contains("expr");
    if (has_value == has_expr) throw VariableError(name, name + ": give it a value or a formula (expr), not both");
    Var var{.type = type, .value = nullptr, .options = {}};
    if (type == "enum") {
      if (!v.contains("options") || !v["options"].is_array() || v["options"].empty())
        throw VariableError(name, name + ": an enum needs its options");
      for (const auto& o : v["options"]) var.options.push_back(o.get<std::string>());
    }
    if (has_expr) {
      if (type != "number" && type != "boolean")
        throw VariableError(name, name + ": only a number or a boolean can be a formula");
      if (!v["expr"].is_string()) throw VariableError(name, name + ": its formula (expr) must be text");
      formulas[name] = v["expr"].get<std::string>();
    } else {
      const json& value = v["value"];
      const bool fits = type == "number"    ? value.is_number()
                        : type == "boolean" ? value.is_boolean()
                                            : value.is_string();
      if (!fits) throw VariableError(name, name + " is a " + type + ", but its value is " + kind_of(value));
      if (type == "enum" && std::ranges::find(var.options, value.get<std::string>()) == var.options.end())
        throw VariableError(name, name + " is \"" + value.get<std::string>() + "\", not one of its options");
      var.value = value;
    }
    vars_[name] = std::move(var);
    defs[name] = v;
    order.push_back(name);
  }

  // Formulas, each after the variables it uses; a loop is an error.
  std::set<std::string, std::less<>> done, visiting;
  std::vector<std::string> path;
  auto resolve = [&](auto& self, const std::string& name) -> void {
    if (done.contains(name)) return;
    if (visiting.contains(name)) {
      std::string loop;
      for (auto it = std::ranges::find(path, name); it != path.end(); ++it) loop += *it + " → ";
      throw VariableError(name, "variables loop: " + loop + name);
    }
    visiting.insert(name);
    path.push_back(name);
    const std::string& text = formulas.at(name);
    for (const auto& used : names_in(text)) {
      auto it = vars_.find(used);
      if (it == vars_.end()) continue;  // evaluate() names the unknown one
      if (it->second.type != "number" && it->second.type != "boolean")
        throw VariableError(name, name + "'s formula uses " + used + ", a " + it->second.type + ": formulas take numbers and booleans");
      if (formulas.contains(used)) self(self, used);
    }
    const double x = evaluate(text, name);
    vars_.at(name).value = vars_.at(name).type == "boolean" ? json(x != 0) : json(x);
    path.pop_back();
    visiting.erase(name);
    done.insert(name);
  };
  for (const auto& name : order)
    if (formulas.contains(name)) resolve(resolve, name);

  // Ranges, now that every number has its value.
  for (const auto& name : order) {
    const auto& var = vars_.at(name);
    if (var.type != "number") continue;
    const double x = var.value.get<double>();
    const json& d = defs.at(name);
    if (d.contains("min") && x < d["min"].get<double>())
      throw VariableError(name, name + " is " + number_text(x) + ", below its minimum " + number_text(d["min"].get<double>()));
    if (d.contains("max") && x > d["max"].get<double>())
      throw VariableError(name, name + " is " + number_text(x) + ", above its maximum " + number_text(d["max"].get<double>()));
    if (d.value("whole", false) && x != std::round(x))
      throw VariableError(name, name + " is " + number_text(x) + ", not a whole number");
  }
}

double Variables::evaluate(const std::string& text, std::string_view where) const {
  std::vector<std::string> names;
  std::vector<double> values;
  for (const auto& [name, var] : vars_) {
    if (var.value.is_null()) continue;  // a formula not yet evaluated (not one this uses)
    if (var.type == "number") values.push_back(var.value.get<double>());
    else if (var.type == "boolean") values.push_back(var.value.get<bool>() ? 1.0 : 0.0);
    else continue;
    names.push_back(name);
  }
  for (const auto& used : names_in(text)) {
    if (vars_.contains(used)) continue;
    const std::string near = nearest(used, vars_);
    throw VariableError(std::string(where), std::string(where) + ": unknown variable \"" + used + "\"" + (near.empty() ? "" : " (" + near + "?)") +
                                                " in " + text);
  }
  try {
    const double x = Expression(text, names)(values);
    if (!std::isfinite(x)) throw VariableError(std::string(where), std::string(where) + ": " + text + " is not a finite number");
    return x;
  } catch (const ExpressionError& err) {
    throw VariableError(std::string(where), std::string(where) + ": " + text + ": " + err.what());
  }
}

json Variables::values() const {
  json out = json::object();
  for (const auto& [name, var] : vars_) out[name] = var.value;
  return out;
}

json Variables::value_for(const json& given, const ParamDef& def, std::string_view where) const {
  if (!is_reference(given)) return given;
  const std::string at = std::string(where) + "." + def.id;
  if (given.size() != 1) throw VariableError(std::string(where), at + ": a reference is {\"var\": name} or {\"expr\": formula}");
  if (given.contains("expr")) {
    if (def.type != "number" && def.type != "boolean")
      throw VariableError(std::string(where), at + " is " + def.type + ": only number and boolean params take a formula");
    if (!given["expr"].is_string()) throw VariableError(std::string(where), at + ": a formula (expr) is text");
    const double x = evaluate(given["expr"].get<std::string>(), at);
    return def.type == "boolean" ? json(x != 0) : json(x);
  }
  if (!given["var"].is_string()) throw VariableError(std::string(where), at + ": a variable is named by its name");
  const auto name = given["var"].get<std::string>();
  auto it = vars_.find(name);
  if (it == vars_.end()) {
    const std::string near = nearest(name, vars_);
    throw VariableError(std::string(where), at + ": unknown variable \"" + name + "\"" + (near.empty() ? "" : " (" + near + "?)"));
  }
  const Var& var = it->second;
  const bool fits = def.type == "number"    ? var.type == "number"
                    : def.type == "boolean" ? var.type == "boolean"
                    : def.type == "enum"    ? var.type == "enum"
                    : def.type == "text"    ? var.type == "text" || var.type == "file"
                    : def.type == "file"    ? var.type == "file" || var.type == "text"
                                            : false;  // a key belongs to its block
  if (!fits) throw VariableError(std::string(where), at + " is " + def.type + ", but " + name + " is a " + var.type);
  if (def.type == "enum" && def.option(var.value.get<std::string>()) < 0) {
    std::string known;
    for (const auto& o : def.options) known += (known.empty() ? "" : ", ") + o;
    throw VariableError(std::string(where), at + ": " + name + " is \"" + var.value.get<std::string>() + "\", not one of: " + known);
  }
  return var.value;
}

json resolve_graph(const json& graph, const Catalog& catalog, const Variables& vars) {
  json out = graph;
  if (!out.is_object() || !out.contains("nodes")) return out;
  for (auto& n : out["nodes"]) {
    if (!n.contains("params") || !n["params"].is_object()) continue;
    const auto* entry = n.contains("type") ? catalog.find(n["type"].get<std::string>()) : nullptr;
    if (!entry) continue;  // an unknown type: the graph's own check reports it
    const auto id = n.value("id", std::string{});
    for (auto& [param, value] : n["params"].items()) {
      if (!is_reference(value)) continue;
      auto def = std::ranges::find(entry->def.params, param, &ParamDef::id);
      if (def == entry->def.params.end()) throw VariableError(id, id + " (" + entry->def.type + ") has no param \"" + param + "\"");
      value = vars.value_for(value, *def, id);
    }
  }
  return out;
}

json resolve_station(const json& station, const Catalog& catalog, const Variables& vars) {
  json out = station;
  if (!out.is_object() || !out.contains("objects")) return out;
  for (auto& o : out["objects"]) {
    if (!o.contains("props") || !o["props"].is_object()) continue;
    const auto* entry = o.contains("type") ? catalog.find_object(o["type"].get<std::string>()) : nullptr;
    if (!entry) continue;
    const auto id = o.value("id", std::string{});
    for (auto& [prop, value] : o["props"].items()) {
      if (!is_reference(value)) continue;
      auto def = std::ranges::find(entry->def.props, prop, &ParamDef::id);
      if (def == entry->def.props.end()) throw VariableError(id, id + " (" + entry->def.type + ") has no property \"" + prop + "\"");
      value = vars.value_for(value, *def, id);
    }
  }
  return out;
}

}  // namespace paperboat::toolkit
