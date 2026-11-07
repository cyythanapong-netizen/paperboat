// IconDef → Modelica Icon(graphics) (squero/0 `set_icon`), the inverse of quantise_icon in icon.cpp.
// Every token becomes one fixed Modelica value, chosen so the importer reads it back as the same
// token (the Wave 2 goldens round-trip). The one lossy corner: text holding {param} comes back
// mono, as the importer always draws parameter text.

#include <cmath>
#include <string>

#include "paperboat/modelica/import.hpp"

namespace paperboat::modelica {
namespace {

using nlohmann::json;

std::string num(double v) {
  if (std::abs(v - std::round(v)) < 1e-9) return std::to_string(static_cast<long long>(std::llround(v)));
  std::string s = std::to_string(v);
  while (s.back() == '0') s.pop_back();
  return s;
}

// The Modelica value for one of the style tokens; throws on an unknown token.
std::string pick(const json& prim, const char* key, std::initializer_list<std::pair<const char*, const char*>> table,
                 const char* fallback) {
  const std::string token = prim.value(key, std::string(fallback));
  for (const auto& [name, value] : table)
    if (token == name) return value;
  throw OmcError(std::string("icon: unknown ") + key + " \"" + token + "\"");
}

struct Frame {
  double x1, y1, w, h;  // Modelica extent
  std::string x(double u) const { return num(x1 + u / 100.0 * w); }
  std::string y(double u) const { return num(y1 + h - u / 100.0 * h); }  // icon y runs down
  std::string pt(const json& p) const { return "{" + x(p.at(0).get<double>()) + ", " + y(p.at(1).get<double>()) + "}"; }
  std::string box(const json& g) const {
    const double ux = g.at("x"), uy = g.at("y"), uw = g.at("w"), uh = g.at("h");
    return "extent={" + pt({ux, uy + uh}) + ", " + pt({ux + uw, uy}) + "}";
  }
};

std::string color(const std::string& stroke) {
  if (stroke == "ink") return "{0, 0, 0}";
  if (stroke == "muted") return "{160, 160, 160}";
  if (stroke == "signal") return "{0, 0, 0}";  // live-state colour never goes into a model
  throw OmcError("icon: unknown stroke \"" + stroke + "\"");
}

}  // namespace

std::string icon_annotation(const json& icon) {
  const int w = icon.value("w", 1);
  const int h = icon.value("h", 1);
  if ((w != 1 && w != 2) || (h != 1 && h != 2) || (w == 2 && h == 2)) throw OmcError("icon: size must be 1x1, 2x1 or 1x2");
  const Frame f{-100.0 * w, -100.0 * h, 200.0 * w, 200.0 * h};
  const double ext_w = f.w;

  std::string out = "Icon(coordinateSystem(extent={{" + num(f.x1) + ", " + num(f.y1) + "}, {" + num(f.x1 + f.w) + ", " +
                    num(f.y1 + f.h) + "}}), graphics={";
  bool first = true;
  for (const json& g : icon.at("g")) {
    const std::string t = g.value("t", "");
    const std::string stroke = color(g.value("stroke", "ink"));
    const std::string weight = pick(g, "weight", {{"thin", "0.1"}, {"normal", "0.25"}, {"wire", "0.5"}}, "normal");
    const std::string pattern = pick(g, "dash", {{"solid", "LinePattern.Solid"}, {"axis", "LinePattern.Dash"}}, "solid");
    const std::string fill = pick(g, "fill",
                                  {{"none", "FillPattern.None"}, {"ink", "FillPattern.Solid"},
                                   {"raised", "FillPattern.Solid"}, {"sunken", "FillPattern.Solid"}},
                                  "none");
    const std::string fill_color = pick(g, "fill", {{"none", "{255, 255, 255}"}, {"ink", "{0, 0, 0}"},
                                                    {"raised", "{255, 255, 255}"}, {"sunken", "{192, 192, 192}"}},
                                        "none");
    const std::string style = "lineColor=" + stroke + ", fillColor=" + fill_color + ", pattern=" + pattern +
                              ", fillPattern=" + fill + ", lineThickness=" + weight;
    std::string item;
    if (t == "line") {
      item = "Line(points={";
      for (std::size_t i = 0; i < g.at("p").size(); ++i) item += (i ? ", " : "") + f.pt(g["p"][i]);
      item += "}, color=" + stroke + ", pattern=" + pattern + ", thickness=" + weight + ")";
    } else if (t == "poly") {
      item = "Polygon(points={";
      for (std::size_t i = 0; i < g.at("p").size(); ++i) item += (i ? ", " : "") + f.pt(g["p"][i]);
      item += "}, " + style + ")";
    } else if (t == "rect") {
      const std::string corner = g.value("corner", "0");
      if (corner != "0" && corner != "sm" && corner != "md") throw OmcError("icon: unknown corner \"" + corner + "\"");
      // The importer reads radius as a share of the extent width: >= 5% is md, any other > 0 is sm.
      const double radius = corner == "md" ? 0.06 * ext_w : corner == "sm" ? 0.02 * ext_w : 0.0;
      item = "Rectangle(" + f.box(g) + ", " + style + ", radius=" + num(radius) + ")";
    } else if (t == "ellipse") {
      item = "Ellipse(" + f.box(g) + ", " + style + ")";
    } else if (t == "arc") {
      item = "Ellipse(" + f.box(g) + ", lineColor=" + stroke + ", pattern=" + pattern +
             ", fillPattern=FillPattern.None, lineThickness=" + weight + ", startAngle=" + num(g.at("a0").get<double>()) +
             ", endAngle=" + num(g.at("a1").get<double>()) + ", closure=EllipseClosure.None)";
    } else if (t == "text") {
      std::string s;
      for (const char c : g.at("s").get<std::string>()) {  // {param} → %param, a literal % → %%
        if (c == '%') s += "%%";
        else if (c == '{') s += '%';
        else if (c != '}') s += c;
      }
      const double x = g.at("x"), y = g.at("y");
      const std::string font = pick(g, "font", {{"sans", "Arial"}, {"mono", "Courier New"}}, "sans");
      item = "Text(extent={" + f.pt({x - 40.0, y + 10.0}) + ", " + f.pt({x + 40.0, y - 10.0}) + "}, textString=" +
             quote(s) + ", fontSize=" + num(g.value("size", 12.0)) + ", textColor=" + stroke + ", fontName=" + quote(font) + ")";
    } else {
      throw OmcError("icon: unknown primitive \"" + t + "\"");
    }
    out += (first ? "" : ", ") + item;
    first = false;
  }
  return out + "})";
}

}  // namespace paperboat::modelica
