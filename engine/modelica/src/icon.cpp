#include "paperboat/modelica/import.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paperboat::modelica {
namespace {

namespace fs = std::filesystem;

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

std::string serialise_value(const OmValue& v) {
  if (v.is_string) return quote(v.atom);
  if (!v.is_list) return v.atom;
  std::string out = "{";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i > 0) out += ", ";
    out += serialise_value(v[i]);
  }
  out += "}";
  return out;
}

// Replaces any `DynamicSelect(staticVal, dynamicVal)` subtree with `staticVal` and sets `found`.
OmValue strip_dynamic_select(const OmValue& v, bool& found) {
  if (v.is_string) return v;
  if (v.is_list) {
    OmValue out;
    out.is_list = true;
    out.items.reserve(v.items.size());
    for (const auto& item : v.items) out.items.push_back(strip_dynamic_select(item, found));
    return out;
  }
  // Named argument `foo = DynamicSelect(a, b)`
  if (auto kv = split_named(v.atom)) {
    const OmValue rhs = parse_value(kv->second);
    bool rhs_found = false;
    const OmValue stripped_rhs = strip_dynamic_select(rhs, rhs_found);
    if (rhs_found) {
      found = true;
      OmValue out;
      out.atom = kv->first + " = " + serialise_value(stripped_rhs);
      return out;
    }
    return v;
  }
  if (auto call = parse_call(v.atom)) {
    if (call->first == "DynamicSelect" && call->second.size() >= 1) {
      found = true;
      return strip_dynamic_select(call->second[0], found);
    }
    bool inner_found = false;
    std::string rebuilt = call->first + "(";
    for (std::size_t i = 0; i < call->second.size(); ++i) {
      if (i > 0) rebuilt += ", ";
      const OmValue sub = strip_dynamic_select(call->second[i], inner_found);
      rebuilt += serialise_value(sub);
    }
    rebuilt += ")";
    if (inner_found) {
      found = true;
      OmValue out;
      out.atom = std::move(rebuilt);
      return out;
    }
  }
  return v;
}

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
  bool valid = false;
};

Rgb parse_rgb(const OmValue& v) {
  if (!v.is_list || v.size() < 3) return {};
  const auto r = parse_num(v[0].atom);
  const auto g = parse_num(v[1].atom);
  const auto b = parse_num(v[2].atom);
  if (!r || !g || !b) return {};
  if (*r < 0.0 || *g < 0.0 || *b < 0.0) return {};  // {-1, -1, -1} means inherit lineColor
  return {.r = static_cast<int>(std::llround(*r)),
          .g = static_cast<int>(std::llround(*g)),
          .b = static_cast<int>(std::llround(*b)),
          .valid = true};
}

// Never returns "signal" (reserved for live run state).
std::string quantise_stroke(const Rgb& c) {
  if (!c.valid) return "ink";
  const double lum = 0.299 * static_cast<double>(c.r) + 0.587 * static_cast<double>(c.g) +
                     0.114 * static_cast<double>(c.b);
  const int max_c = std::max({c.r, c.g, c.b});
  const int min_c = std::min({c.r, c.g, c.b});
  const int chroma = max_c - min_c;
  if ((chroma <= 30 && lum >= 100.0) || lum >= 180.0) return "muted";
  return "ink";
}

std::string quantise_fill(const Rgb& c, std::string_view pattern, ImportNotice& notice) {
  const std::string p = trim(pattern);
  if (p.empty() || p == "None" || p.ends_with(".None")) return "none";
  if (p != "Solid" && !p.ends_with(".Solid")) ++notice.simplified;
  if (!c.valid) return "raised";
  const double lum = 0.299 * static_cast<double>(c.r) + 0.587 * static_cast<double>(c.g) +
                     0.114 * static_cast<double>(c.b);
  if (lum < 85.0) return "ink";
  if (lum >= 220.0) return "raised";
  return "sunken";
}

std::string quantise_weight(double thickness) {
  if (thickness >= 0.5) return "wire";
  if (thickness >= 0.25) return "normal";
  return "thin";
}

std::string quantise_dash(std::string_view pattern) {
  const std::string p = trim(pattern);
  if (p.find("Dash") != std::string::npos || p.find("Dot") != std::string::npos) return "axis";
  return "solid";
}

bool is_line_none(std::string_view pattern) {
  const std::string p = trim(pattern);
  return p == "None" || p.ends_with(".None");
}

int snap5(double v) {
  const int s = static_cast<int>(std::llround(v / 5.0)) * 5;
  return std::clamp(s, 0, 100);
}

std::array<int, 2> map_point(double lx, double ly, double ox, double oy, double rot_deg, const IconExtent& ext) {
  const double rad = rot_deg * (std::numbers::pi / 180.0);
  const double c = std::cos(rad);
  const double s = std::sin(rad);
  const double wx = ox + lx * c - ly * s;
  const double wy = oy + lx * s + ly * c;

  const double ex1 = std::min(ext.x1, ext.x2);
  const double ex2 = std::max(ext.x1, ext.x2);
  const double ey1 = std::min(ext.y1, ext.y2);
  const double ey2 = std::max(ext.y1, ext.y2);
  const double w = (ex2 - ex1 > 1e-6) ? (ex2 - ex1) : 200.0;
  const double h = (ey2 - ey1 > 1e-6) ? (ey2 - ey1) : 200.0;

  const double ux = ((wx - ex1) / w) * 100.0;
  const double uy = ((ey2 - wy) / h) * 100.0;
  return {snap5(ux), snap5(uy)};
}

bool parse_vec2(const OmValue& v, double& x, double& y) {
  if (!v.is_list || v.size() < 2) return false;
  const auto px = parse_num(v[0].atom);
  const auto py = parse_num(v[1].atom);
  if (!px || !py) return false;
  x = *px;
  y = *py;
  return true;
}

bool parse_box(const OmValue& v, double& x1, double& y1, double& x2, double& y2) {
  if (!v.is_list || v.size() < 2) return false;
  return parse_vec2(v[0], x1, y1) && parse_vec2(v[1], x2, y2);
}

std::map<std::string, OmValue> named_args(const OmValue& args) {
  std::map<std::string, OmValue> out;
  for (const auto& item : args.items) {
    if (item.is_string) continue;
    if (auto kv = split_named(item.atom)) {
      try {
        out.emplace(kv->first, parse_value(kv->second));
      } catch (...) {
      }
    }
  }
  return out;
}

const OmValue* arg_at(const OmValue& args, const std::map<std::string, OmValue>& named, std::string_view key,
                      std::size_t pos) {
  if (auto it = named.find(std::string(key)); it != named.end()) return &it->second;
  if (pos < args.size() && (args[pos].is_string || !split_named(args[pos].atom))) return &args[pos];
  return nullptr;
}

std::string substitute_text_params(std::string_view raw, bool& has_param) {
  std::string out;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '%' && i + 1 < raw.size()) {
      if (raw[i + 1] == '%') {
        out += '%';
        ++i;
        continue;
      }
      if (std::isalpha(static_cast<unsigned char>(raw[i + 1])) || raw[i + 1] == '_') {
        std::size_t j = i + 1;
        while (j < raw.size() &&
               (std::isalnum(static_cast<unsigned char>(raw[j])) || raw[j] == '_'))
          ++j;
        const std::string_view param = raw.substr(i + 1, j - (i + 1));
        out += '{';
        out += param;
        out += '}';
        has_param = true;
        i = j - 1;
        continue;
      }
    }
    out += raw[i];
  }
  return out;
}

double nearest_font_size(double modelica_pt, double box_h_100, bool has_param) {
  constexpr std::array<double, 5> kSizes = {12.0, 12.5, 13.0, 15.0, 30.0};
  double target = modelica_pt;
  if (target <= 0.0) {
    if (has_param) return 12.0;
    target = std::clamp(box_h_100 * 0.35, 12.0, 30.0);
  }
  double best = kSizes[0];
  double best_diff = std::abs(target - best);
  for (const double s : kSizes) {
    const double d = std::abs(target - s);
    if (d < best_diff) {
      best = s;
      best_diff = d;
    }
  }
  return best;
}

void attach_style(nlohmann::json& prim, const std::string& stroke, const std::string& weight,
                  const std::string& dash, const std::string& fill, bool allow_fill) {
  if (stroke != "ink") prim["stroke"] = stroke;
  if (weight != "normal") prim["weight"] = weight;
  if (dash != "solid") prim["dash"] = dash;
  if (allow_fill && fill != "none") prim["fill"] = fill;
}

IconExtent extract_extent(const OmValue& icon_anno, IconExtent fallback) {
  if (!icon_anno.is_list || icon_anno.size() < 4) return fallback;
  const auto x1 = parse_num(icon_anno[0].atom);
  const auto y1 = parse_num(icon_anno[1].atom);
  const auto x2 = parse_num(icon_anno[2].atom);
  const auto y2 = parse_num(icon_anno[3].atom);
  if (x1 && y1 && x2 && y2) return {.x1 = *x1, .y1 = *y1, .x2 = *x2, .y2 = *y2};
  return fallback;
}

const OmValue* extract_graphics_list(const OmValue& icon_anno) {
  if (!icon_anno.is_list) return nullptr;
  // omc getIconAnnotation returns 9-tuple with graphics list at [8]; also accept a direct list of graphics.
  if (icon_anno.size() >= 9 && icon_anno[8].is_list) return &icon_anno[8];
  for (const auto& item : icon_anno.items) {
    if (!item.atom.empty() && parse_call(item.atom).has_value()) return &icon_anno;
  }
  return nullptr;
}

}  // namespace

nlohmann::json quantise_icon(const std::vector<OmValue>& icon_annotations, const std::vector<IconPort>& ports,
                             ImportNotice& notice, IconExtent* out_extent) {
  IconExtent final_ext{};
  for (const auto& anno : icon_annotations) final_ext = extract_extent(anno, final_ext);
  if (out_extent) *out_extent = final_ext;

  const double ext_w = std::abs(final_ext.x2 - final_ext.x1);
  const double ext_h = std::abs(final_ext.y2 - final_ext.y1);
  int plate_w = 1;
  int plate_h = 1;
  if (ext_h > 1e-6 && (ext_w / ext_h) >= 1.75 - 1e-9) {
    plate_w = 2;
    plate_h = 1;
  } else if (ext_w > 1e-6 && (ext_h / ext_w) >= 1.75 - 1e-9) {
    plate_w = 1;
    plate_h = 2;
  }

  nlohmann::json g = nlohmann::json::array();

  for (const auto& anno : icon_annotations) {
    const IconExtent ext = extract_extent(anno, final_ext);
    const double cur_ext_w = (std::abs(ext.x2 - ext.x1) > 1e-6) ? std::abs(ext.x2 - ext.x1) : 200.0;
    const OmValue* glist = extract_graphics_list(anno);
    if (!glist) continue;

    for (const auto& raw_item : glist->items) {
      bool had_dyn = false;
      const OmValue item = strip_dynamic_select(raw_item, had_dyn);
      if (had_dyn) {
        notice.dropped.push_back("DynamicSelect");
        ++notice.simplified;
      }

      auto call = parse_call(item.atom);
      if (!call) continue;
      const std::string& kind = call->first;
      const OmValue& args = call->second;

      if (kind == "Bitmap") {
        notice.dropped.push_back("Bitmap");
        ++notice.simplified;
        continue;
      }

      const auto named = named_args(args);
      if (const OmValue* vis = arg_at(args, named, "visible", 0)) {
        if (trim(vis->atom) == "false") continue;
      }

      double ox = 0.0;
      double oy = 0.0;
      if (const OmValue* orig = arg_at(args, named, "origin", 1)) parse_vec2(*orig, ox, oy);
      double rot = 0.0;
      if (const OmValue* r = arg_at(args, named, "rotation", 2)) rot = parse_num(r->atom).value_or(0.0);

      if (kind == "Line") {
        // Line(vis[0], orig[1], rot[2], points[3], color[4], pattern[5], thickness[6], arrow[7], arrowSize[8], smooth[9])
        const OmValue* pts = arg_at(args, named, "points", 3);
        if (!pts || !pts->is_list) continue;
        Rgb col{};
        if (const OmValue* c = arg_at(args, named, "color", 4)) col = parse_rgb(*c);
        std::string pat = "LinePattern.Solid";
        if (const OmValue* p = arg_at(args, named, "pattern", 5)) pat = p->atom;
        if (is_line_none(pat)) continue;
        double thick = 0.25;
        if (const OmValue* th = arg_at(args, named, "thickness", 6)) thick = parse_num(th->atom).value_or(0.25);
        if (const OmValue* sm = arg_at(args, named, "smooth", 9)) {
          if (sm->atom.find("Bezier") != std::string::npos) ++notice.simplified;
        }

        nlohmann::json p_arr = nlohmann::json::array();
        std::optional<std::array<int, 2>> prev;
        for (const auto& pv : pts->items) {
          double lx = 0.0;
          double ly = 0.0;
          if (!parse_vec2(pv, lx, ly)) continue;
          const auto mapped = map_point(lx, ly, ox, oy, rot, ext);
          if (!prev || *prev != mapped) {
            p_arr.push_back({mapped[0], mapped[1]});
            prev = mapped;
          }
        }
        if (p_arr.size() < 2) {
          if (pts->size() >= 2) ++notice.simplified;
          continue;
        }
        nlohmann::json prim = {{"t", "line"}, {"p", std::move(p_arr)}};
        attach_style(prim, quantise_stroke(col), quantise_weight(thick), quantise_dash(pat), "none", false);
        g.push_back(std::move(prim));
        continue;
      }

      // Shared style positions for Polygon / Rectangle / Ellipse / Text:
      // lineColor[3], fillColor[4], pattern[5], fillPattern[6], thickness[7]
      Rgb line_col{};
      if (const OmValue* lc = arg_at(args, named, "lineColor", 3)) line_col = parse_rgb(*lc);
      Rgb fill_col{};
      if (const OmValue* fc = arg_at(args, named, "fillColor", 4)) fill_col = parse_rgb(*fc);
      std::string line_pat = "LinePattern.Solid";
      if (const OmValue* lp = arg_at(args, named, "pattern", 5)) line_pat = lp->atom;
      std::string fill_pat = "FillPattern.None";
      if (const OmValue* fp = arg_at(args, named, "fillPattern", 6)) fill_pat = fp->atom;
      double thick = 0.25;
      if (const OmValue* th = arg_at(args, named, "thickness", 7)) thick = parse_num(th->atom).value_or(0.25);

      if (kind == "Polygon") {
        // Polygon(..., thickness[7], points[8], smooth[9])
        const OmValue* pts = arg_at(args, named, "points", 8);
        if (!pts || !pts->is_list) continue;
        if (const OmValue* sm = arg_at(args, named, "smooth", 9)) {
          if (sm->atom.find("Bezier") != std::string::npos) ++notice.simplified;
        }
        const std::string fill = quantise_fill(fill_col, fill_pat, notice);
        if (is_line_none(line_pat) && fill == "none") continue;
        const std::string stroke = is_line_none(line_pat) ? (fill == "ink" ? "ink" : "muted") : quantise_stroke(line_col);
        const std::string weight = is_line_none(line_pat) ? "thin" : quantise_weight(thick);

        std::vector<std::array<int, 2>> pts_vec;
        for (const auto& pv : pts->items) {
          double lx = 0.0;
          double ly = 0.0;
          if (!parse_vec2(pv, lx, ly)) continue;
          const auto mapped = map_point(lx, ly, ox, oy, rot, ext);
          if (pts_vec.empty() || pts_vec.back() != mapped) pts_vec.push_back(mapped);
        }
        if (pts_vec.size() >= 3 && pts_vec.back() == pts_vec.front()) pts_vec.pop_back();
        if (pts_vec.size() < 2) {
          if (pts->size() >= 2) ++notice.simplified;
          continue;
        }
        nlohmann::json p_arr = nlohmann::json::array();
        for (const auto& pt : pts_vec) p_arr.push_back({pt[0], pt[1]});
        nlohmann::json prim = {{"t", "poly"}, {"p", std::move(p_arr)}};
        attach_style(prim, stroke, weight, quantise_dash(line_pat), fill, true);
        g.push_back(std::move(prim));
        continue;
      }

      if (kind == "Rectangle") {
        // Rectangle(..., thickness[7], borderPattern[8], extent[9], radius[10])
        const OmValue* box = arg_at(args, named, "extent", 9);
        double bx1 = 0.0;
        double by1 = 0.0;
        double bx2 = 0.0;
        double by2 = 0.0;
        if (!box || !parse_box(*box, bx1, by1, bx2, by2)) continue;
        if (const OmValue* bp = arg_at(args, named, "borderPattern", 8)) {
          const std::string bpat = trim(bp->atom);
          if (!bpat.empty() && bpat != "None" && !bpat.ends_with(".None")) ++notice.simplified;
        }
        double radius = 0.0;
        if (const OmValue* rad = arg_at(args, named, "radius", 10)) radius = parse_num(rad->atom).value_or(0.0);

        const std::string fill = quantise_fill(fill_col, fill_pat, notice);
        if (is_line_none(line_pat) && fill == "none") continue;
        const std::string stroke = is_line_none(line_pat) ? (fill == "ink" ? "ink" : "muted") : quantise_stroke(line_col);
        const std::string weight = is_line_none(line_pat) ? "thin" : quantise_weight(thick);

        const auto p0 = map_point(bx1, by1, ox, oy, rot, ext);
        const auto p1 = map_point(bx2, by2, ox, oy, rot, ext);
        const int rx = std::min(p0[0], p1[0]);
        const int ry = std::min(p0[1], p1[1]);
        const int rw = std::abs(p1[0] - p0[0]);
        const int rh = std::abs(p1[1] - p0[1]);
        if (rw == 0 && rh == 0) {
          ++notice.simplified;
          continue;
        }

        nlohmann::json prim = {{"t", "rect"}, {"x", rx}, {"y", ry}, {"w", rw}, {"h", rh}};
        const double scaled_r = (radius / cur_ext_w) * 100.0;
        if (scaled_r >= 5.0) prim["corner"] = "md";
        else if (scaled_r > 0.0) prim["corner"] = "sm";
        attach_style(prim, stroke, weight, quantise_dash(line_pat), fill, true);
        g.push_back(std::move(prim));
        continue;
      }

      if (kind == "Ellipse") {
        // Ellipse(..., thickness[7], extent[8], startAngle[9], endAngle[10], closure[11])
        const OmValue* box = arg_at(args, named, "extent", 8);
        double bx1 = 0.0;
        double by1 = 0.0;
        double bx2 = 0.0;
        double by2 = 0.0;
        if (!box || !parse_box(*box, bx1, by1, bx2, by2)) continue;
        double a0 = 0.0;
        double a1 = 360.0;
        if (const OmValue* sa = arg_at(args, named, "startAngle", 9)) a0 = parse_num(sa->atom).value_or(0.0);
        if (const OmValue* ea = arg_at(args, named, "endAngle", 10)) a1 = parse_num(ea->atom).value_or(360.0);

        const std::string fill = quantise_fill(fill_col, fill_pat, notice);
        if (is_line_none(line_pat) && fill == "none") continue;
        const std::string stroke = is_line_none(line_pat) ? (fill == "ink" ? "ink" : "muted") : quantise_stroke(line_col);
        const std::string weight = is_line_none(line_pat) ? "thin" : quantise_weight(thick);

        const auto p0 = map_point(bx1, by1, ox, oy, rot, ext);
        const auto p1 = map_point(bx2, by2, ox, oy, rot, ext);
        int rx = std::min(p0[0], p1[0]);
        int ry = std::min(p0[1], p1[1]);
        int rw = std::abs(p1[0] - p0[0]);
        int rh = std::abs(p1[1] - p0[1]);
        if (rw == 0 && std::abs(bx2 - bx1) > 1e-6) {
          rw = 5;
          if (rx + rw > 100) rx = 95;
        }
        if (rh == 0 && std::abs(by2 - by1) > 1e-6) {
          rh = 5;
          if (ry + rh > 100) ry = 95;
        }
        if (rw == 0 && rh == 0) {
          ++notice.simplified;
          continue;
        }

        const bool full = std::abs(std::abs(a1 - a0) - 360.0) < 1e-3 || std::abs(a1 - a0) >= 360.0;
        nlohmann::json prim;
        if (full) {
          prim = {{"t", "ellipse"}, {"x", rx}, {"y", ry}, {"w", rw}, {"h", rh}};
          attach_style(prim, stroke, weight, quantise_dash(line_pat), fill, true);
        } else {
          prim = {{"t", "arc"}, {"x", rx}, {"y", ry}, {"w", rw}, {"h", rh}, {"a0", a0 + rot}, {"a1", a1 + rot}};
          attach_style(prim, stroke, weight, quantise_dash(line_pat), "none", false);
        }
        g.push_back(std::move(prim));
        continue;
      }

      if (kind == "Text") {
        // Text(..., thickness[7], extent[8], textString[9], fontSize[10], textColor[11], fontName[12], ...)
        const OmValue* box = arg_at(args, named, "extent", 8);
        const OmValue* txt = arg_at(args, named, "textString", 9);
        double bx1 = 0.0;
        double by1 = 0.0;
        double bx2 = 0.0;
        double by2 = 0.0;
        if (!box || !txt || !parse_box(*box, bx1, by1, bx2, by2)) continue;

        const std::string raw_s = txt->is_string ? txt->atom : unquote_all(txt->atom);
        const std::string trimmed_s = trim(raw_s);
        // `%name` is the external instance label, already rendered by CompactBlock's plate caption.
        if (trimmed_s.empty() || trimmed_s == "%name") continue;

        bool has_param = false;
        const std::string s = substitute_text_params(raw_s, has_param);

        const auto center = map_point(0.5 * (bx1 + bx2), 0.5 * (by1 + by2), ox, oy, rot, ext);
        const double cur_ext_h = (std::abs(ext.y2 - ext.y1) > 1e-6) ? std::abs(ext.y2 - ext.y1) : 200.0;
        const double box_h_100 = (std::abs(by2 - by1) / cur_ext_h) * 100.0;

        double font_pt = 0.0;
        if (const OmValue* fs_arg = arg_at(args, named, "fontSize", 10))
          font_pt = parse_num(fs_arg->atom).value_or(0.0);
        const double tok_size = nearest_font_size(font_pt, box_h_100, has_param);

        Rgb txt_col{};
        if (const OmValue* tc = arg_at(args, named, "textColor", 11)) txt_col = parse_rgb(*tc);
        const std::string stroke = txt_col.valid ? quantise_stroke(txt_col) : quantise_stroke(line_col);

        std::string font = has_param ? "mono" : "sans";
        if (const OmValue* fn = arg_at(args, named, "fontName", 12)) {
          const std::string fname = fn->is_string ? fn->atom : unquote_all(fn->atom);
          if (fname.find("Mono") != std::string::npos || fname.find("mono") != std::string::npos ||
              fname.find("Courier") != std::string::npos)
            font = "mono";
        }

        nlohmann::json prim = {{"t", "text"}, {"x", center[0]}, {"y", center[1]}, {"s", s}};
        if (font != "sans") prim["font"] = font;
        if (std::abs(tok_size - std::round(tok_size)) < 1e-9) {
          const int int_size = static_cast<int>(std::llround(tok_size));
          if (int_size != 12) prim["size"] = int_size;
        } else {
          prim["size"] = tok_size;
        }
        if (stroke != "ink") prim["stroke"] = stroke;
        g.push_back(std::move(prim));
        continue;
      }

      // Unknown graphic record
      notice.dropped.push_back(kind);
      ++notice.simplified;
    }
  }

  nlohmann::json icon = nlohmann::json::object();
  if (plate_w != 1) icon["w"] = plate_w;
  if (plate_h != 1) icon["h"] = plate_h;
  icon["g"] = std::move(g);
  if (!ports.empty()) {
    nlohmann::json p_arr = nlohmann::json::array();
    for (const auto& p : ports) p_arr.push_back(to_json(p));
    icon["ports"] = std::move(p_arr);
  }
  return icon;
}

namespace {

std::set<std::string> loaded_top_classes(Omc& omc) {
  std::set<std::string> out;
  for (const auto& v : parse_value(omc.call("getClassNames()")).items) {
    if (!v.atom.empty()) out.insert(v.atom);
  }
  return out;
}

void ensure_msl_if_needed(Omc& omc, std::string_view text) {
  if (text.find("Modelica") == std::string_view::npos) return;
  if (!loaded_top_classes(omc).contains("Modelica")) {
    if (trim(omc.call("loadModel(Modelica)")) != "true") {
      throw OmcError("loadModel(Modelica) failed: " + omc.errors());
    }
  }
}

void collect_icon_hierarchy(Omc& omc, const std::string& cls, std::set<std::string>& visited,
                            std::vector<OmValue>& icons) {
  if (!visited.insert(cls).second) return;
  const OmValue bases = parse_value(omc.call("getInheritedClasses(" + cls + ")"));
  for (const auto& b : bases.items) {
    if (b.atom.empty()) continue;
    std::string base = trim(b.atom);
    // Qualify relative base name if needed
    const std::string r = unquote_all(parse_value(omc.call("getClassRestriction(" + base + ")")).atom);
    if (r.empty() || r.find("Error") != std::string::npos) {
      std::string prefix = cls;
      while (!prefix.empty()) {
        const std::string cand = prefix + "." + base;
        const std::string cr = unquote_all(parse_value(omc.call("getClassRestriction(" + cand + ")")).atom);
        if (!cr.empty() && cr.find("Error") == std::string::npos) {
          base = cand;
          break;
        }
        const auto dot = prefix.rfind('.');
        if (dot == std::string::npos) break;
        prefix.erase(dot);
      }
    }
    collect_icon_hierarchy(omc, base, visited, icons);
  }
  const OmValue anno = parse_value(omc.call("getIconAnnotation(" + cls + ")"));
  if (anno.is_list && !anno.items.empty()) icons.push_back(anno);
}

std::string sniff_first_class(const std::string& mo_text) {
  std::istringstream in(mo_text);
  std::string tok;
  while (in >> tok) {
    if (tok == "within") {
      std::getline(in, tok, ';');
      continue;
    }
    if (tok == "model" || tok == "block" || tok == "connector" || tok == "class") {
      std::string name;
      if (in >> name) {
        while (!name.empty() && !std::isalnum(static_cast<unsigned char>(name.back())) && name.back() != '_')
          name.pop_back();
        if (!name.empty()) return name;
      }
    }
  }
  return {};
}

}  // namespace

ImportedModel import_class(Omc& omc, std::string_view class_name) {
  const std::string cls = trim(class_name);
  if (cls.empty()) throw OmcError("empty Modelica class name");
  ensure_msl_if_needed(omc, cls);

  const std::string r = unquote_all(parse_value(omc.call("getClassRestriction(" + cls + ")")).atom);
  if (r.empty() || r.find("Error") != std::string::npos) {
    const std::string err = omc.errors();
    throw OmcError("no Modelica class \"" + cls + "\"" + (err.empty() ? "" : ": " + err));
  }

  ImportedModel out;
  out.model = cls;

  std::set<std::string> visited;
  std::vector<OmValue> icon_annos;
  collect_icon_hierarchy(omc, cls, visited, icon_annos);

  IconExtent extent{};
  for (const auto& anno : icon_annos) extent = extract_extent(anno, extent);

  resolve_connectors(omc, cls, out.raw_ports, out.connectors, out.notice);
  const std::vector<IconPort> ports = quantise_ports(out.raw_ports, out.notice, extent);
  out.icon = quantise_icon(icon_annos, ports, out.notice, &extent);
  out.extent = extent;
  return out;
}

ImportedModel import_model(Omc& omc, const fs::path& source, std::string_view class_name) {
  if (source.empty()) return import_class(omc, class_name);
  if (!fs::is_regular_file(source)) throw OmcError("no such file: " + source.string());

  std::ifstream in(source, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  const std::string mo_text = buf.str();

  ensure_msl_if_needed(omc, mo_text);
  const std::set<std::string> before = loaded_top_classes(omc);
  if (trim(omc.call("loadFile(" + quote(fs::absolute(source).string()) + ")")) != "true") {
    throw OmcError("loadFile failed for " + source.string() + ": " + omc.errors());
  }

  std::string target = trim(class_name);
  if (target.empty()) {
    std::vector<std::string> fresh;
    for (const auto& n : loaded_top_classes(omc))
      if (!before.contains(n)) fresh.push_back(n);
    if (fresh.size() == 1) {
      target = fresh.front();
    } else if (fresh.empty()) {
      target = sniff_first_class(mo_text);
    }
    if (target.empty()) {
      std::string list;
      for (const auto& n : fresh) list += " " + n;
      throw OmcError(fresh.empty() ? "no class found in " + source.string() : "pick one with --class:" + list);
    }
  }
  return import_class(omc, target);
}

}  // namespace paperboat::modelica
