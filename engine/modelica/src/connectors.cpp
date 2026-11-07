#include "paperboat/modelica/import.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paperboat::modelica {
namespace {

std::string trim(std::string_view s) {
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

std::string unquote_all(std::string s) {
  for (;;) {
    s = trim(s);
    if (s.size() >= 4 && s.starts_with("\\\"") && s.ends_with("\\\"")) {
      s = s.substr(2, s.size() - 4);
      continue;
    }
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s = s.substr(1, s.size() - 2);
      continue;
    }
    break;
  }
  return s;
}

std::string short_name(std::string_view cls) {
  const auto dot = cls.rfind('.');
  return std::string(dot == std::string_view::npos ? cls : cls.substr(dot + 1));
}

std::optional<double> parse_num(std::string_view text) {
  const std::string t = trim(text);
  if (t.empty() || t == "-") return std::nullopt;
  double v = 0.0;
  const char* end = t.data() + t.size();
  auto [p, ec] = std::from_chars(t.data(), end, v);
  if (ec != std::errc() || p != end) return std::nullopt;
  return v;
}

std::optional<std::pair<std::string, std::string>> split_named(std::string_view atom) {
  int depth = 0;
  for (std::size_t i = 0; i < atom.size(); ++i) {
    const char c = atom[i];
    if (c == '"') {
      for (++i; i < atom.size() && atom[i] != '"'; ++i)
        if (atom[i] == '\\') ++i;
    } else if (c == '(' || c == '{') {
      ++depth;
    } else if (c == ')' || c == '}') {
      if (depth > 0) --depth;
    } else if (c == '=' && depth == 0) {
      if (i > 0 && (atom[i - 1] == '=' || atom[i - 1] == '!' || atom[i - 1] == '<' || atom[i - 1] == '>')) continue;
      if (i + 1 < atom.size() && atom[i + 1] == '=') continue;
      return std::make_pair(trim(atom.substr(0, i)), trim(atom.substr(i + 1)));
    }
  }
  return std::nullopt;
}

bool parse_transform_record(const OmValue& args, RawPort& port) {
  bool got_extent = false;
  double ox = 0.0;
  double oy = 0.0;
  double x1 = -10.0;
  double y1 = -10.0;
  double x2 = 10.0;
  double y2 = 10.0;
  double rot = 0.0;

  for (const auto& item : args.items) {
    if (auto kv = split_named(item.atom)) {
      const OmValue val = parse_value(kv->second);
      if (kv->first == "origin" && val.is_list && val.size() >= 2) {
        ox = parse_num(val[0].atom).value_or(0.0);
        oy = parse_num(val[1].atom).value_or(0.0);
      } else if (kv->first == "extent" && val.is_list && val.size() >= 2 && val[0].is_list && val[1].is_list &&
                 val[0].size() >= 2 && val[1].size() >= 2) {
        const auto ex1 = parse_num(val[0][0].atom);
        const auto ey1 = parse_num(val[0][1].atom);
        const auto ex2 = parse_num(val[1][0].atom);
        const auto ey2 = parse_num(val[1][1].atom);
        if (ex1 && ey1 && ex2 && ey2) {
          x1 = *ex1;
          y1 = *ey1;
          x2 = *ex2;
          y2 = *ey2;
          got_extent = true;
        }
      } else if (kv->first == "rotation") {
        rot = parse_num(val.atom).value_or(0.0);
      }
    }
  }
  // Positional transformation({ox, oy}, {{x1, y1}, {x2, y2}}, rot)
  if (!got_extent && args.size() >= 2 && args[0].is_list && args[1].is_list) {
    if (args[0].size() >= 2 && args[1].size() >= 2 && args[1][0].is_list && args[1][1].is_list &&
        args[1][0].size() >= 2 && args[1][1].size() >= 2) {
      const auto ex1 = parse_num(args[1][0][0].atom);
      const auto ey1 = parse_num(args[1][0][1].atom);
      const auto ex2 = parse_num(args[1][1][0].atom);
      const auto ey2 = parse_num(args[1][1][1].atom);
      if (ex1 && ey1 && ex2 && ey2) {
        ox = parse_num(args[0][0].atom).value_or(0.0);
        oy = parse_num(args[0][1].atom).value_or(0.0);
        x1 = *ex1;
        y1 = *ey1;
        x2 = *ex2;
        y2 = *ey2;
        if (args.size() >= 3) rot = parse_num(args[2].atom).value_or(0.0);
        got_extent = true;
      }
    }
  }
  if (!got_extent) return false;
  port.has_placement = true;
  port.origin_x = ox;
  port.origin_y = oy;
  port.x1 = x1;
  port.y1 = y1;
  port.x2 = x2;
  port.y2 = y2;
  port.rotation = rot;
  return true;
}

int snap5(double v) {
  const int s = static_cast<int>(std::llround(v / 5.0)) * 5;
  return std::clamp(s, 0, 100);
}

std::string clean_condition(std::string s) {
  s = unquote_all(std::move(s));
  if (s.starts_with("if ")) s = trim(s.substr(3));
  else if (s.starts_with("if(")) s = trim(s.substr(2));
  while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
    int depth = 0;
    bool wraps_all = true;
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
      if (s[i] == '(') ++depth;
      else if (s[i] == ')') {
        --depth;
        if (depth == 0) {
          wraps_all = false;
          break;
        }
      }
    }
    if (!wraps_all) break;
    s = trim(s.substr(1, s.size() - 2));
  }
  return s;
}

struct ResolverState {
  Omc& omc;
  std::map<std::string, std::string> type_cache{};
  std::map<std::string, std::string> unit_cache{};
  std::map<std::string, bool> is_conn_cache{};
  std::map<std::string, std::string> restriction_cache{};

  std::string restriction(const std::string& cls) {
    if (auto it = restriction_cache.find(cls); it != restriction_cache.end()) return it->second;
    std::string r;
    try {
      r = unquote_all(parse_value(omc.call("getClassRestriction(" + cls + ")")).atom);
    } catch (...) {
      r.clear();
    }
    restriction_cache[cls] = r;
    return r;
  }

  bool class_exists(const std::string& cls) {
    const std::string r = restriction(cls);
    return !r.empty() && r.find("Error") == std::string::npos;
  }

  std::string resolve_type(std::string_view scope, std::string_view raw) {
    const std::string key = std::string(scope) + "::" + std::string(raw);
    if (auto it = type_cache.find(key); it != type_cache.end()) return it->second;
    std::string t = trim(raw);
    if (t.starts_with('.')) t.erase(0, 1);

    std::string found = t;
    if (!class_exists(t)) {
      std::string prefix(scope);
      while (!prefix.empty()) {
        const std::string candidate = prefix + "." + t;
        if (class_exists(candidate)) {
          found = candidate;
          break;
        }
        const auto dot = prefix.rfind('.');
        if (dot == std::string::npos) break;
        prefix.erase(dot);
      }
    }
    type_cache[key] = found;
    return found;
  }

  bool is_connector(const std::string& full_type) {
    if (auto it = is_conn_cache.find(full_type); it != is_conn_cache.end()) return it->second;
    const std::string ans = trim(omc.call("isConnector(" + full_type + ")"));
    bool ok = (ans == "true");
    if (!ok) {
      const std::string r = restriction(full_type);
      ok = (r == "connector" || r == "expandable connector");
    }
    is_conn_cache[full_type] = ok;
    return ok;
  }

  std::vector<std::string> inherited(const std::string& cls) {
    std::vector<std::string> out;
    const OmValue v = parse_value(omc.call("getInheritedClasses(" + cls + ")"));
    for (const auto& item : v.items) {
      if (!item.atom.empty()) out.push_back(resolve_type(cls, item.atom));
    }
    return out;
  }

  void hierarchy_dfs(const std::string& cls, std::set<std::string>& visited, std::vector<std::string>& order) {
    if (!visited.insert(cls).second) return;
    for (const auto& base : inherited(cls)) hierarchy_dfs(base, visited, order);
    order.push_back(cls);
  }

  std::string unit_for_type(const std::string& full_type) {
    if (full_type.empty() || full_type == "Real" || full_type == "Integer" || full_type == "Boolean" ||
        full_type == "String")
      return {};
    if (auto it = unit_cache.find(full_type); it != unit_cache.end()) return it->second;
    unit_cache[full_type] = {};  // guard cycles
    std::string u = unquote_all(omc.call("getDerivedClassModifierValue(" + full_type + ", unit)"));
    if (u.empty()) {
      for (const auto& base : inherited(full_type)) {
        u = unit_for_type(base);
        if (!u.empty()) break;
      }
    }
    unit_cache[full_type] = u;
    return u;
  }

  std::string unit_for_component(const std::string& owner_cls, const std::string& declaring_cls,
                                 const std::string& var_id, const std::string& var_type) {
    for (const auto* cls : {&owner_cls, &declaring_cls}) {
      std::string u = unquote_all(omc.call("getComponentModifierValue(" + *cls + ", " + var_id + ".unit)"));
      if (!u.empty()) return u;
    }
    return unit_for_type(var_type);
  }
};

struct BuiltinMatch {
  bool matched = false;
  ConnectorDef def{};
  std::string port_variant{};
};

BuiltinMatch match_builtin_connector(std::string_view full_class, std::string_view sname, std::string_view kind,
                                     const std::vector<VarUnit>& potential, const std::vector<VarUnit>& flow,
                                     const std::vector<VarUnit>& stream) {
  BuiltinMatch m;
  m.def.kind = std::string(kind);

  if (kind == "expandable") {
    m.matched = true;
    m.def.handle = "square";
    m.def.size = 8;
    m.def.weight = 1.2;
    return m;
  }

  const bool in_msl = full_class.starts_with("Modelica.");

  // Electrical pins & multiphase plugs
  if (sname == "Pin" || sname == "PositivePin" || sname == "NegativePin" || sname == "Plug" ||
      sname == "PositivePlug" || sname == "NegativePlug") {
    m.matched = true;
    m.def.kind = "acausal";
    m.def.handle = "square";
    m.def.size = 8;
    m.def.weight = 1.2;
    if (sname == "PositivePin" || sname == "PositivePlug") {
      m.def.alt = "filled";
      m.port_variant = "filled";
    } else if (sname == "NegativePin" || sname == "NegativePlug") {
      m.port_variant = "hollow";
    }
    return m;
  }

  // Mechanical rotational / translational flanges
  if (sname == "Flange_a" || sname == "Flange_b" || sname == "Support") {
    m.matched = true;
    m.def.kind = "acausal";
    m.def.handle = "square";
    m.def.size = 10;
    m.def.weight = 1.8;
    if (sname == "Flange_a") {
      m.def.alt = "filled";
      m.port_variant = "filled";
    } else if (sname == "Flange_b") {
      m.port_variant = "hollow";
    }
    return m;
  }

  // Thermal ports
  if (sname == "HeatPort" || sname == "HeatPort_a" || sname == "HeatPort_b") {
    m.matched = true;
    m.def.kind = "acausal";
    m.def.handle = "diamond";
    m.def.size = 8;
    m.def.weight = 1.2;
    m.def.dash = "dashed";
    if (sname == "HeatPort_a") {
      m.def.alt = "filled";
      m.port_variant = "filled";
    } else if (sname == "HeatPort_b") {
      m.port_variant = "hollow";
    }
    return m;
  }

  // Fluid ports
  if (sname == "FluidPort" || sname == "FluidPort_a" || sname == "FluidPort_b") {
    m.matched = true;
    m.def.kind = "acausal";
    m.def.handle = "ring2";
    m.def.size = 12;
    m.def.weight = 2.0;
    if (sname == "FluidPort_a") {
      m.def.alt = "filled";
      m.port_variant = "filled";
    } else if (sname == "FluidPort_b") {
      m.port_variant = "hollow";
    }
    return m;
  }

  // Causal block connectors (MSL RealInput/RealOutput, BooleanInput/Output, IntegerInput/Output, etc.)
  if (kind == "input" && (in_msl || sname == "RealInput" || sname == "BooleanInput" || sname == "IntegerInput" ||
                          sname == "ComplexInput")) {
    m.matched = true;
    m.def.kind = "input";
    m.def.handle = "triangle";
    m.def.size = 8;
    m.def.weight = 1.2;
    return m;
  }
  if (kind == "output" && (in_msl || sname == "RealOutput" || sname == "BooleanOutput" || sname == "IntegerOutput" ||
                           sname == "ComplexOutput")) {
    m.matched = true;
    m.def.kind = "output";
    m.def.handle = "circle";
    m.def.size = 8;
    m.def.weight = 1.2;
    m.def.alt = "filled";
    return m;
  }

  // Unknown user-defined connector: nearest built-in handle by kind and domain, recorded in notice.
  m.matched = false;
  if (kind == "input") {
    m.def.kind = "input";
    m.def.handle = "triangle";
    m.def.size = 8;
    m.def.weight = 1.2;
  } else if (kind == "output") {
    m.def.kind = "output";
    m.def.handle = "circle";
    m.def.size = 8;
    m.def.weight = 1.2;
    m.def.alt = "filled";
  } else {
    auto has_unit = [](const std::vector<VarUnit>& vars, std::string_view u) {
      return std::ranges::any_of(vars, [&](const VarUnit& v) { return v.unit == u; });
    };
    m.def.kind = "acausal";
    if (!stream.empty() || has_unit(flow, "kg/s") || full_class.find("Fluid") != std::string_view::npos) {
      m.def.handle = "ring2";
      m.def.size = 12;
      m.def.weight = 2.0;
    } else if ((has_unit(potential, "K") || has_unit(potential, "degC")) &&
               (flow.empty() || has_unit(flow, "W") || full_class.find("Thermal") != std::string_view::npos)) {
      m.def.handle = "diamond";
      m.def.size = 8;
      m.def.weight = 1.2;
      m.def.dash = "dashed";
    } else if (has_unit(flow, "N.m") || has_unit(flow, "N") || has_unit(potential, "rad") || has_unit(potential, "m")) {
      m.def.handle = "square";
      m.def.size = 10;
      m.def.weight = 1.8;
    } else {
      m.def.handle = "square";
      m.def.size = 8;
      m.def.weight = 1.2;
    }
    if (sname.ends_with("_a")) {
      m.def.alt = "filled";
      m.port_variant = "filled";
    } else if (sname.ends_with("_b")) {
      m.port_variant = "hollow";
    }
  }
  return m;
}

}  // namespace

std::optional<std::pair<std::string, OmValue>> parse_call(std::string_view atom) {
  const std::string t = trim(atom);
  const auto lparen = t.find('(');
  if (lparen == std::string::npos || lparen == 0 || t.back() != ')') return std::nullopt;
  const std::string name = trim(std::string_view(t).substr(0, lparen));
  for (const char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return std::nullopt;
  }
  try {
    OmValue args = parse_value(std::string_view(t).substr(lparen));
    return std::make_pair(name, std::move(args));
  } catch (...) {
    return std::nullopt;
  }
}

bool parse_placement(const OmValue& annotations, RawPort& port) {
  for (const auto& item : annotations.items) {
    auto call = parse_call(item.atom);
    if (!call || call->first != "Placement") continue;
    const OmValue& args = call->second;

    // 1. Named / nested Placement(iconTransformation(...), transformation(...)): prefer iconTransformation.
    std::optional<OmValue> icon_tr;
    std::optional<OmValue> diag_tr;
    for (const auto& arg : args.items) {
      std::string rhs = arg.atom;
      if (auto kv = split_named(arg.atom)) {
        if (kv->first == "iconTransformation" || kv->first == "transformation") {
          rhs = kv->first + "(" + kv->second + ")";
          if (auto inner = parse_call(kv->second)) rhs = kv->second;
        }
      }
      if (auto sub = parse_call(rhs)) {
        if (sub->first == "iconTransformation") icon_tr = sub->second;
        else if (sub->first == "transformation") diag_tr = sub->second;
      }
    }
    if (icon_tr && parse_transform_record(*icon_tr, port)) return true;
    if (diag_tr && parse_transform_record(*diag_tr, port)) return true;

    // 2. omc positional 15-tuple:
    //    Placement(vis[0], ox[1], oy[2], x1[3], y1[4], x2[5], y2[6], rot[7],
    //              iox[8], ioy[9], ix1[10], iy1[11], ix2[12], iy2[13], irot[14])
    if (args.size() >= 15) {
      const auto ix1 = parse_num(args[10].atom);
      const auto iy1 = parse_num(args[11].atom);
      const auto ix2 = parse_num(args[12].atom);
      const auto iy2 = parse_num(args[13].atom);
      if (ix1 && iy1 && ix2 && iy2) {
        port.has_placement = true;
        port.origin_x = parse_num(args[8].atom).value_or(0.0);
        port.origin_y = parse_num(args[9].atom).value_or(0.0);
        port.x1 = *ix1;
        port.y1 = *iy1;
        port.x2 = *ix2;
        port.y2 = *iy2;
        port.rotation = parse_num(args[14].atom).value_or(0.0);
        return true;
      }
    }
    if (args.size() >= 8) {
      const auto x1 = parse_num(args[3].atom);
      const auto y1 = parse_num(args[4].atom);
      const auto x2 = parse_num(args[5].atom);
      const auto y2 = parse_num(args[6].atom);
      if (x1 && y1 && x2 && y2) {
        port.has_placement = true;
        port.origin_x = parse_num(args[1].atom).value_or(0.0);
        port.origin_y = parse_num(args[2].atom).value_or(0.0);
        port.x1 = *x1;
        port.y1 = *y1;
        port.x2 = *x2;
        port.y2 = *y2;
        port.rotation = parse_num(args[7].atom).value_or(0.0);
        return true;
      }
    }
  }
  return false;
}

std::vector<IconPort> quantise_ports(const std::vector<RawPort>& raw, ImportNotice& notice, IconExtent extent) {
  double ex1 = std::min(extent.x1, extent.x2);
  double ex2 = std::max(extent.x1, extent.x2);
  double ey1 = std::min(extent.y1, extent.y2);
  double ey2 = std::max(extent.y1, extent.y2);
  if (ex2 - ex1 < 1e-6) {
    ex1 = -100.0;
    ex2 = 100.0;
  }
  if (ey2 - ey1 < 1e-6) {
    ey1 = -100.0;
    ey2 = 100.0;
  }
  const double cx = 0.5 * (ex1 + ex2);
  const double cy = 0.5 * (ey1 + ey2);
  const double half_w = 0.5 * (ex2 - ex1);
  const double half_h = 0.5 * (ey2 - ey1);

  std::vector<IconPort> out(raw.size());
  std::vector<int> orig_at(raw.size(), 50);

  for (std::size_t i = 0; i < raw.size(); ++i) {
    const RawPort& r = raw[i];
    IconPort& p = out[i];
    p.id = r.id;
    p.class_name = r.class_name;
    p.variant = r.variant;
    p.array = r.array;
    p.condition = r.condition;

    if (!r.has_placement) {
      if (r.kind == "output") p.side = "right";
      else if (r.kind == "input") p.side = "left";
      else p.side = (r.variant == "hollow") ? "right" : "left";
      p.at = 50;
      continue;
    }

    // Snap rotation to 90° steps; keep mirroring via extent coordinates (x2 < x1 or y2 < y1).
    double deg = std::fmod(r.rotation, 360.0);
    if (deg < 0.0) deg += 360.0;
    const int rot90 = (static_cast<int>(std::llround(deg / 90.0)) * 90) % 360;

    const double lcx = 0.5 * (r.x1 + r.x2);
    const double lcy = 0.5 * (r.y1 + r.y2);
    double rx = lcx;
    double ry = lcy;
    if (rot90 == 90) {
      rx = -lcy;
      ry = lcx;
    } else if (rot90 == 180) {
      rx = -lcx;
      ry = -lcy;
    } else if (rot90 == 270) {
      rx = lcy;
      ry = -lcx;
    }

    const double wx = r.origin_x + rx;
    const double wy = r.origin_y + ry;
    const double nx = (wx - cx) / half_w;
    const double ny = (wy - cy) / half_h;

    const double anx = std::abs(nx);
    const double any = std::abs(ny);
    bool vertical_edge = anx > any;
    if (std::abs(anx - any) <= 1e-9) {
      if (rot90 == 90 || rot90 == 270) vertical_edge = false;
      else vertical_edge = true;
    }

    if (vertical_edge) {
      if (std::abs(nx) <= 1e-9) p.side = (r.kind == "output") ? "right" : "left";
      else p.side = (nx < 0.0) ? "left" : "right";
      p.at = snap5(((ey2 - wy) / (ey2 - ey1)) * 100.0);
    } else {
      p.side = (ny >= 0.0) ? "top" : "bottom";
      p.at = snap5(((wx - ex1) / (ex2 - ex1)) * 100.0);
    }
    orig_at[i] = p.at;
  }

  // Distribute unplaced ports evenly across free slots on their side first (not counted as collisions).
  for (const char* side : {"left", "right", "top", "bottom"}) {
    std::vector<std::size_t> unplaced;
    std::set<int> used;
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (out[i].side != side) continue;
      if (raw[i].has_placement) used.insert(out[i].at);
      else unplaced.push_back(i);
    }
    if (unplaced.empty()) continue;

    std::vector<int> free_slots;
    for (int s = 5; s <= 95; s += 5)
      if (!used.contains(s)) free_slots.push_back(s);
    if (free_slots.size() < unplaced.size()) {
      free_slots.clear();
      for (int s = 0; s <= 100; s += 5)
        if (!used.contains(s)) free_slots.push_back(s);
    }
    const std::size_t m = unplaced.size();
    for (std::size_t j = 0; j < m; ++j) {
      if (free_slots.empty()) {
        out[unplaced[j]].at = 50;
      } else {
        const double frac = static_cast<double>(j + 1) / static_cast<double>(m + 1);
        int idx = static_cast<int>(std::llround(frac * static_cast<double>(free_slots.size() + 1))) - 1;
        idx = std::clamp(idx, 0, static_cast<int>(free_slots.size()) - 1);
        out[unplaced[j]].at = free_slots[static_cast<std::size_t>(idx)];
      }
      orig_at[unplaced[j]] = out[unplaced[j]].at;
    }
  }

  // Spread placed ports that collide on the same (side, at) evenly across free slots on that side.
  for (const char* side : {"left", "right", "top", "bottom"}) {
    std::map<int, std::vector<std::size_t>> by_slot;
    for (std::size_t i = 0; i < out.size(); ++i) {
      if (out[i].side == side) by_slot[out[i].at].push_back(i);
    }
    bool has_collision = false;
    std::set<int> occupied;
    for (const auto& [slot, indices] : by_slot) {
      if (indices.size() == 1) occupied.insert(slot);
      else has_collision = true;
    }
    if (!has_collision) continue;

    for (const auto& [slot, indices] : by_slot) {
      if (indices.size() <= 1) continue;
      std::vector<int> free_slots;
      for (int s = 5; s <= 95; s += 5)
        if (!occupied.contains(s)) free_slots.push_back(s);
      if (free_slots.size() < indices.size()) {
        free_slots.clear();
        for (int s = 0; s <= 100; s += 5)
          if (!occupied.contains(s)) free_slots.push_back(s);
      }
      const std::size_t g = indices.size();
      int prev_pick = -1;
      for (std::size_t j = 0; j < g; ++j) {
        int chosen = slot;
        if (!free_slots.empty()) {
          const double frac = static_cast<double>(j + 1) / static_cast<double>(g + 1);
          int pick = static_cast<int>(std::llround(frac * static_cast<double>(free_slots.size() + 1))) - 1;
          const int max_pick = static_cast<int>(free_slots.size()) - static_cast<int>(g - j);
          if (max_pick >= 0) pick = std::clamp(pick, prev_pick + 1, max_pick);
          else pick = std::clamp(pick, 0, static_cast<int>(free_slots.size()) - 1);
          prev_pick = pick;
          chosen = free_slots[static_cast<std::size_t>(pick)];
        }
        occupied.insert(chosen);
        const std::size_t idx = indices[j];
        out[idx].at = chosen;
        const int dist = std::abs(chosen - orig_at[idx]);
        if (dist > 0) notice.moved.push_back({.id = out[idx].id, .distance = dist});
      }
    }
  }

  return out;
}

void resolve_connectors(Omc& omc, std::string_view class_name, std::vector<RawPort>& out_ports,
                        std::map<std::string, ConnectorDef>& out_connectors, ImportNotice& notice) {
  ResolverState st{omc};
  const std::string root = st.resolve_type("", class_name);

  std::set<std::string> visited_classes;
  std::vector<std::string> class_order;
  st.hierarchy_dfs(root, visited_classes, class_order);

  std::set<std::string> seen_port_ids;
  std::map<std::string, BuiltinMatch> resolved_conns;

  for (const auto& cls : class_order) {
    const OmValue comps = parse_value(omc.call("getComponents(" + cls + ")"));
    const OmValue annos = parse_value(omc.call("getComponentAnnotations(" + cls + ")"));

    for (std::size_t i = 0; i < comps.size(); ++i) {
      const OmValue& c = comps[i];
      if (!c.is_list || c.size() < 11) continue;
      const std::string raw_type = c[0].atom;
      const std::string comp_id = c[1].atom;
      const std::string visibility = c[3].atom;
      const std::string inner_outer = c[9].atom;
      const std::string comp_causality = c[10].atom;

      if (visibility == "protected" || inner_outer == "outer" || comp_id.empty()) continue;
      if (seen_port_ids.contains(comp_id)) continue;

      const std::string full_type = st.resolve_type(cls, raw_type);
      if (!st.is_connector(full_type)) continue;
      seen_port_ids.insert(comp_id);

      const std::string sname = short_name(full_type);

      auto conn_it = resolved_conns.find(full_type);
      if (conn_it == resolved_conns.end()) {
        std::set<std::string> c_vis;
        std::vector<std::string> c_order;
        st.hierarchy_dfs(full_type, c_vis, c_order);

        std::string kind = "acausal";
        if (st.restriction(full_type) == "expandable connector") {
          kind = "expandable";
        } else if (comp_causality == "input" || comp_causality == "output") {
          kind = comp_causality;
        } else {
          // Check short class definition (`connector RealInput = input Real`) across connector hierarchy.
          for (auto it = c_order.rbegin(); it != c_order.rend(); ++it) {
            const std::string src = unquote_all(parse_value(omc.call("list(" + *it + ")")).atom);
            if (src.find("= input ") != std::string::npos || src.find("=input ") != std::string::npos) {
              kind = "input";
              break;
            }
            if (src.find("= output ") != std::string::npos || src.find("=output ") != std::string::npos) {
              kind = "output";
              break;
            }
          }
        }

        std::vector<VarUnit> potential;
        std::vector<VarUnit> flow;
        std::vector<VarUnit> stream;
        std::set<std::string> seen_vars;
        bool all_input = true;
        bool all_output = true;
        bool any_var = false;

        for (const auto& base_conn : c_order) {
          const OmValue sub_comps = parse_value(omc.call("getComponents(" + base_conn + ")"));
          for (const auto& sc : sub_comps.items) {
            if (!sc.is_list || sc.size() < 11) continue;
            const std::string v_vis = sc[3].atom;
            const std::string v_var = sc[8].atom;
            if (v_vis == "protected" || v_var == "parameter" || v_var == "constant") continue;
            const std::string v_id = sc[1].atom;
            if (v_id.empty() || !seen_vars.insert(v_id).second) continue;

            const std::string v_type = st.resolve_type(base_conn, sc[0].atom);
            const bool is_flow = (sc[5].atom == "true");
            const bool is_stream = (sc[6].atom == "true");
            const std::string v_caus = sc[10].atom;

            any_var = true;
            if (is_flow || is_stream || v_caus != "input") all_input = false;
            if (is_flow || is_stream || v_caus != "output") all_output = false;

            const std::string u = st.unit_for_component(full_type, base_conn, v_id, v_type);
            if (is_stream) stream.push_back({.id = v_id, .unit = u});
            else if (is_flow) flow.push_back({.id = v_id, .unit = u});
            else potential.push_back({.id = v_id, .unit = u});
          }
        }

        if (kind == "acausal" && any_var && flow.empty() && stream.empty()) {
          if (all_input) kind = "input";
          else if (all_output) kind = "output";
        }

        BuiltinMatch bm = match_builtin_connector(full_type, sname, kind, potential, flow, stream);
        if (!bm.matched) ++notice.simplified;
        if (bm.def.kind == "acausal") {
          bm.def.potential = std::move(potential);
          bm.def.flow = std::move(flow);
          bm.def.stream = std::move(stream);
        }
        conn_it = resolved_conns.emplace(full_type, std::move(bm)).first;
      }

      const BuiltinMatch& bm = conn_it->second;
      out_connectors[sname] = bm.def;

      RawPort rp;
      rp.id = comp_id;
      rp.class_name = sname;
      rp.full_class = full_type;
      rp.kind = bm.def.kind;
      rp.variant = bm.port_variant;

      // Array dimension: `{n}` at comp[11]
      if (c.size() > 11 && c[11].is_list && !c[11].items.empty()) {
        std::string dim = unquote_all(c[11][0].atom);
        if (!dim.empty()) rp.array = dim;
      }

      // Conditional `if <expr>`: 1-based index in `cls`
      const std::string cond_raw = omc.call("getNthComponentCondition(" + cls + ", " + std::to_string(i + 1) + ")");
      rp.condition = clean_condition(cond_raw);

      if (i < annos.size()) parse_placement(annos[i], rp);
      out_ports.push_back(std::move(rp));
    }
  }
}

nlohmann::json to_json(const ConnectorDef& def) {
  nlohmann::json j = {{"kind", def.kind}, {"handle", def.handle}, {"size", def.size}};
  if (std::abs(def.weight - std::round(def.weight)) < 1e-9) {
    j["weight"] = static_cast<int>(std::llround(def.weight));
  } else {
    j["weight"] = def.weight;
  }
  if (!def.alt.empty()) j["alt"] = def.alt;
  if (!def.dash.empty() && def.dash != "solid") j["dash"] = def.dash;

  auto vars_json = [](const std::vector<VarUnit>& list) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& v : list) arr.push_back({{"id", v.id}, {"unit", v.unit}});
    return arr;
  };
  if (!def.potential.empty()) j["potential"] = vars_json(def.potential);
  if (!def.flow.empty()) j["flow"] = vars_json(def.flow);
  if (!def.stream.empty()) j["stream"] = vars_json(def.stream);
  return j;
}

nlohmann::json to_json(const IconPort& port) {
  nlohmann::json j = {{"id", port.id}, {"class", port.class_name}, {"side", port.side}, {"at", port.at}};
  if (!port.variant.empty()) j["variant"] = port.variant;
  if (!port.array.empty()) j["array"] = port.array;
  if (!port.condition.empty()) j["if"] = port.condition;
  return j;
}

nlohmann::json to_json(const ImportNotice& notice) {
  nlohmann::json moved = nlohmann::json::array();
  for (const auto& m : notice.moved) moved.push_back({{"id", m.id}, {"distance", m.distance}});
  return {{"simplified", notice.simplified}, {"dropped", notice.dropped}, {"moved", std::move(moved)}};
}

nlohmann::json ImportedModel::to_json() const {
  nlohmann::json conns = nlohmann::json::object();
  for (const auto& [name, def] : connectors) conns[name] = modelica::to_json(def);
  return {{"icon", icon}, {"connectors", std::move(conns)}, {"notice", modelica::to_json(notice)}};
}

}  // namespace paperboat::modelica
