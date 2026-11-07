// engine/modelica (#160): the omc bridge's allow-list and answer parser, the expression
// translator, and generate()'s path choice against a real omc.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "paperboat/modelica/generate.hpp"
#include "paperboat/modelica/import.hpp"
#include "paperboat/modelica/omc.hpp"
#include "paperboat/modelica/squero.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace mo = paperboat::modelica;
namespace tk = paperboat::toolkit;
namespace fs = std::filesystem;
using nlohmann::json;

TEST_CASE("omc answers parse into lists, strings and atoms", "[modelica]") {
  const auto v = mo::parse_value(R"({{Real, x, "a \"q\"", {}}, (true, "/tmp/f.xml"), Placement(true, -1)})");
  REQUIRE(v.is_list);
  REQUIRE(v.size() == 3);
  CHECK(v[0][1].atom == "x");
  CHECK(v[0][2].is_string);
  CHECK(v[0][2].atom == "a \"q\"");
  CHECK(v[0][3].is_list);
  CHECK(v[1][0].atom == "true");
  CHECK(v[1][1].atom == "/tmp/f.xml");
  CHECK(v[2].atom == "Placement(true, -1)");
  CHECK_THROWS_AS(mo::parse_value("{a, b"), mo::OmcError);
}

TEST_CASE("squero/0 passes only allow-listed calls", "[modelica]") {
  CHECK(mo::allowed("getClassNames()"));
  CHECK(mo::allowed("getClassNames(recursive=true)"));
  CHECK(mo::allowed(R"(loadFile("/tmp/a b;c.mo"))"));
  CHECK(mo::allowed("addComponent(r1, Modelica.Electrical.Analog.Basic.Resistor, M, "
                    "annotate=Placement(transformation(extent={{-10,-10},{10,10}})))"));
  CHECK_FALSE(mo::allowed(R"(system("ls"))"));
  CHECK_FALSE(mo::allowed(R"(runScript("x.mos"))"));
  CHECK_FALSE(mo::allowed(R"(getClassNames(); system("ls"))"));
  CHECK_FALSE(mo::allowed(R"(loadFile(OpenModelica.Scripting.system("ls")))"));
  CHECK_FALSE(mo::allowed(R"(loadFile(readFile("x")))"));
  CHECK_FALSE(mo::allowed("x := getVersion()"));
  CHECK_FALSE(mo::allowed("getVersion"));
  CHECK_FALSE(mo::allowed(R"(loadFile("unterminated))"));

  mo::Omc omc;  // never started: handle() refuses before calling it
  CHECK(mo::handle(omc, R"j({"id": 7, "op": "omc", "expr": "system(\"ls\")"})j").find(R"("ok":false)") != std::string::npos);
  CHECK(mo::handle(omc, R"({"id": 7, "op": "nope"})").find("unknown op") != std::string::npos);
}

TEST_CASE("Modelica expressions become C++", "[modelica]") {
  auto name = [](std::string_view n, bool der) { return (der ? "d_" : "") + std::string(n == "load.w" ? "w" : n); };
  CHECK(mo::to_cpp("1/2", name) == "(1.0 / 2.0)");  // Modelica's / is real division
  CHECK(mo::to_cpp("-a^2", name) == "(-std::pow(a, 2.0))");
  CHECK(mo::to_cpp("(-shaft.tau) / load.J", name) == "((-shaft.tau) / load.J)");
  CHECK(mo::to_cpp("der(load.w) + time", name) == "(d_w + t)");
  CHECK(mo::to_cpp("if x > 0.5 then -x elseif x < 0 then 0 else 2.0 * x", name) ==
        "((x > 0.5) ? (-x) : ((x < 0.0) ? 0.0 : (2.0 * x)))");
  CHECK(mo::to_cpp("noEvent(abs(x)) + max(a, 1e-3)", name) == "(std::abs(x) + std::max(a, 1e-3))");
  CHECK(mo::to_cpp("t.y[1] * .5", name) == "(t.y[1] * 0.5)");
  CHECK_THROWS_AS(mo::to_cpp("pre(z) + 1", name), mo::GenerateError);
  CHECK_THROWS_AS(mo::to_cpp("a .+ b", name), mo::GenerateError);
}

TEST_CASE("backend names come from the class's short name", "[modelica]") {
  CHECK(mo::backend_name("DCMotor") == "dc-motor");
  CHECK(mo::backend_name("Modelica.Mechanics.Rotational.Components.Inertia") == "inertia");
  CHECK(mo::backend_name("PIDController") == "pid-controller");
  CHECK(mo::backend_name("mass_spring") == "mass-spring");
}

TEST_CASE("generate picks the ODE path when it can and says why when it can't", "[modelica][omc]") {
  if (std::system("command -v omc >/dev/null 2>&1") != 0) SKIP("omc not installed");
  const fs::path dir = fs::temp_directory_path() / "paperboat-modelica-test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    std::ofstream(dir / "Decay.mo") << R"(model Decay "First-order decay with a probe"
  parameter Real k(unit = "1/s") = 2 "Rate";
  Modelica.Blocks.Interfaces.RealInput u;
  Modelica.Blocks.Interfaces.RealOutput y(unit = "m");
  Real x(start = 1, fixed = true, unit = "m");
  Real rate = k * x annotation(__paperboat(probe = true));
equation
  der(x) = u - k * x;
  y = x;
end Decay;
)";
    std::ofstream(dir / "Bounce.mo") << R"(model Bounce
  Real h(start = 1, fixed = true); Real v(start = 0, fixed = true);
equation
  der(h) = v; der(v) = -9.81;
  when h < 0 then reinit(v, -0.8 * pre(v)); end when;
end Bounce;
)";
  }
  mo::Omc omc;
  const auto r = mo::generate(omc, {.source = dir / "Decay.mo", .out = dir / "decay-cpp"});
  CHECK(r.path == "ode");
  CHECK(r.name == "decay");
  const std::string domain = [&] {
    std::ifstream in(dir / "decay-cpp" / "domain" / "decay.cpp");
    return std::string(std::istreambuf_iterator<char>(in), {});
  }();
  CHECK(domain.find("class Decay final : public paperboat::Model") != std::string::npos);
  CHECK(domain.find(R"({.id = "k", .default_value = 2.0, .unit = "1/s"})") != std::string::npos);
  CHECK(domain.find(R"({.id = "x_start", .default_value = 1.0, .unit = "m"})") != std::string::npos);
  CHECK(domain.find(R"(.outputs = {{"y", "signal", "m"}, {"rate", "signal", ""}})") != std::string::npos);
  CHECK(domain.find(".view = {.icon = nlohmann::json::parse(") != std::string::npos);
  CHECK(domain.find("registry.add_connector(\"decay\", \"RealInput\"") != std::string::npos);
  CHECK(domain.find("registry.add_connector(\"decay\", \"RealOutput\"") != std::string::npos);
  CHECK(fs::is_regular_file(dir / "decay-cpp" / "descriptor.toml"));
  CHECK(fs::is_regular_file(dir / "decay-cpp" / "modelica" / "Decay.mo"));
  tk::Registry decay_reg = tk::builtins();
  decay_reg.add(tk::StationTemplate{
      .name = "decay/decay",
      .def = {.type = "decay",
              .label = "Decay",
              .block = tk::BlockDef{.type = "decay", .inputs = {{"u"}}, .outputs = {{"y"}, {"rate"}}}},
      .make = {},
  });
  const auto generated_cat = tk::load_descriptor_file(dir / "decay-cpp" / "descriptor.toml", decay_reg);
  REQUIRE(generated_cat.connectors.is_object());
  CHECK(generated_cat.connectors.contains("RealInput"));
  CHECK(generated_cat.connectors.contains("RealOutput"));
  CHECK(generated_cat.connectors["RealInput"]["handle"] == "triangle");
  CHECK(generated_cat.connectors["RealInput"]["kind"] == "input");
  CHECK(generated_cat.connectors["RealOutput"]["handle"] == "circle");
  CHECK(generated_cat.connectors["RealOutput"]["kind"] == "output");

  CHECK_THROWS_WITH(mo::generate(omc, {.source = dir / "Bounce.mo", .out = dir / "bounce-cpp", .path = "ode"}),
                    Catch::Matchers::ContainsSubstring("events"));
  CHECK(mo::check(omc, dir / "Bounce.mo", "Bounce").ok);
  fs::remove_all(dir);
}

namespace {

bool on_grid(const json& v) {
  return v.is_number_integer() && v.get<int>() >= 0 && v.get<int>() <= 100 && (v.get<int>() % 5) == 0;
}

void check_contract_tokens(const json& doc) {
  const std::string raw = doc.dump();
  CHECK(raw.find('#') == std::string::npos);
  CHECK(raw.find("rgb(") == std::string::npos);
  CHECK(raw.find("\"d\":") == std::string::npos);

  REQUIRE(doc.contains("icon"));
  REQUIRE(doc.contains("connectors"));
  REQUIRE(doc.contains("notice"));

  const json& icon = doc.at("icon");
  const int w = icon.value("w", 1);
  const int h = icon.value("h", 1);
  CHECK((w == 1 || w == 2));
  CHECK((h == 1 || h == 2));
  CHECK_FALSE((w == 2 && h == 2));

  static const std::set<std::string> kStrokes = {"ink", "muted"};  // importer never emits "signal"
  static const std::set<std::string> kWeights = {"thin", "normal", "wire"};
  static const std::set<std::string> kDashes = {"solid", "axis"};
  static const std::set<std::string> kFills = {"none", "ink", "raised", "sunken"};
  static const std::set<std::string> kCorners = {"0", "sm", "md"};
  static const std::set<std::string> kFonts = {"sans", "mono"};
  static const std::set<double> kSizes = {12.0, 12.5, 13.0, 15.0, 30.0};
  static const std::set<std::string> kSides = {"left", "right", "top", "bottom"};
  static const std::set<std::string> kVariants = {"hollow", "filled"};
  static const std::set<std::string> kKinds = {"acausal", "input", "output", "expandable"};
  static const std::set<std::string> kHandles = {"square", "diamond", "ring2", "triangle", "circle"};
  static const std::set<int> kConnSizes = {8, 10, 12};

  REQUIRE(icon.at("g").is_array());
  for (const auto& p : icon.at("g")) {
    if (p.contains("stroke")) CHECK(kStrokes.contains(p.at("stroke").get<std::string>()));
    if (p.contains("weight")) CHECK(kWeights.contains(p.at("weight").get<std::string>()));
    if (p.contains("dash")) CHECK(kDashes.contains(p.at("dash").get<std::string>()));
    if (p.contains("fill")) CHECK(kFills.contains(p.at("fill").get<std::string>()));
    const std::string t = p.at("t").get<std::string>();
    if (t == "line" || t == "poly") {
      REQUIRE(p.at("p").is_array());
      CHECK(p.at("p").size() >= 2);
      for (const auto& pt : p.at("p")) {
        REQUIRE(pt.size() == 2);
        CHECK(on_grid(pt[0]));
        CHECK(on_grid(pt[1]));
      }
    } else if (t == "rect") {
      CHECK(on_grid(p.at("x")));
      CHECK(on_grid(p.at("y")));
      CHECK(on_grid(p.at("w")));
      CHECK(on_grid(p.at("h")));
      if (p.contains("corner")) CHECK(kCorners.contains(p.at("corner").get<std::string>()));
    } else if (t == "ellipse" || t == "arc") {
      CHECK(on_grid(p.at("x")));
      CHECK(on_grid(p.at("y")));
      CHECK(on_grid(p.at("w")));
      CHECK(on_grid(p.at("h")));
      if (t == "arc") {
        CHECK(p.at("a0").is_number());
        CHECK(p.at("a1").is_number());
      }
    } else if (t == "text") {
      CHECK(on_grid(p.at("x")));
      CHECK(on_grid(p.at("y")));
      CHECK(p.at("s").is_string());
      if (p.contains("font")) CHECK(kFonts.contains(p.at("font").get<std::string>()));
      if (p.contains("size")) CHECK(kSizes.contains(p.at("size").get<double>()));
    } else {
      FAIL("unexpected primitive type: " + t);
    }
  }

  if (icon.contains("ports")) {
    for (const auto& port : icon.at("ports")) {
      CHECK_FALSE(port.at("id").get<std::string>().empty());
      CHECK(doc.at("connectors").contains(port.at("class").get<std::string>()));
      CHECK(kSides.contains(port.at("side").get<std::string>()));
      CHECK(on_grid(port.at("at")));
      if (port.contains("variant")) CHECK(kVariants.contains(port.at("variant").get<std::string>()));
    }
  }

  for (const auto& [name, c] : doc.at("connectors").items()) {
    CHECK_FALSE(name.empty());
    CHECK(kKinds.contains(c.at("kind").get<std::string>()));
    CHECK(kHandles.contains(c.at("handle").get<std::string>()));
    CHECK(kConnSizes.contains(c.at("size").get<int>()));
    const double wt = c.at("weight").get<double>();
    CHECK((std::abs(wt - 1.2) < 1e-9 || std::abs(wt - 1.8) < 1e-9 || std::abs(wt - 2.0) < 1e-9));
    if (c.contains("alt")) CHECK(kVariants.contains(c.at("alt").get<std::string>()));
    if (c.contains("dash")) CHECK((c.at("dash") == "solid" || c.at("dash") == "dashed"));
  }
}

json read_golden(const std::string& stem) {
  const fs::path p = fs::path(PAPERBOAT_TEST_DATA) / "modelica" / (stem + ".icon.json");
  std::ifstream in(p);
  REQUIRE(in.good());
  json doc;
  in >> doc;
  return doc;
}

}  // namespace

TEST_CASE("checked-in MSL icon goldens obey contract §3 and §4 token tables", "[modelica]") {
  for (const std::string stem : {"Resistor", "Inductor", "Capacitor", "Ground", "Inertia"}) {
    INFO(stem);
    check_contract_tokens(read_golden(stem));
  }

  const json resistor = read_golden("Resistor");
  const auto& rports = resistor["icon"]["ports"];
  REQUIRE(rports.size() == 3);
  CHECK(rports[0] == json{{"id", "p"}, {"class", "PositivePin"}, {"side", "left"}, {"at", 50}, {"variant", "filled"}});
  CHECK(rports[1] == json{{"id", "n"}, {"class", "NegativePin"}, {"side", "right"}, {"at", 50}, {"variant", "hollow"}});
  CHECK(rports[2] == json{{"id", "heatPort"}, {"class", "HeatPort_a"}, {"side", "bottom"}, {"at", 50},
                          {"variant", "filled"}, {"if", "useHeatPort"}});
  CHECK(resistor["connectors"]["PositivePin"]["potential"][0] == json{{"id", "v"}, {"unit", "V"}});
  CHECK(resistor["connectors"]["PositivePin"]["flow"][0] == json{{"id", "i"}, {"unit", "A"}});
  CHECK(resistor["connectors"]["HeatPort_a"]["dash"] == "dashed");

  const json ground = read_golden("Ground");
  REQUIRE(ground["icon"]["ports"].size() == 1);
  CHECK(ground["icon"]["ports"][0] == json{{"id", "p"}, {"class", "Pin"}, {"side", "top"}, {"at", 50}});

  const json inertia = read_golden("Inertia");
  const auto& iports = inertia["icon"]["ports"];
  REQUIRE(iports.size() == 2);
  CHECK(iports[0] == json{{"id", "flange_a"}, {"class", "Flange_a"}, {"side", "left"}, {"at", 50}, {"variant", "filled"}});
  CHECK(iports[1] == json{{"id", "flange_b"}, {"class", "Flange_b"}, {"side", "right"}, {"at", 50}, {"variant", "hollow"}});
  CHECK(inertia["connectors"]["Flange_a"]["size"] == 10);
  CHECK(inertia["connectors"]["Flange_a"]["weight"] == 1.8);
  CHECK(inertia["notice"]["simplified"] == 3);

  const json inductor = read_golden("Inductor");
  CHECK(inductor["notice"]["simplified"] == 4);
}

TEST_CASE("iconTransformation quantises edge slots, snaps rotation, and spreads collisions", "[modelica]") {
  // Prefer iconTransformation over diagram transformation.
  mo::RawPort placed{.id = "p", .class_name = "Pin"};
  const auto annos = mo::parse_value(
      "{Placement(transformation(extent={{90,-10},{110,10}}), "
      "iconTransformation(origin={0,20}, extent={{-110,-10},{-90,10}}, rotation=12))}");
  REQUIRE(mo::parse_placement(annos, placed));
  CHECK(placed.has_placement);
  CHECK(placed.origin_y == 20.0);
  CHECK(placed.x1 == -110.0);

  // Rotation snaps to 90° (85° -> 90° anticlockwise turns (-100, 0) into (0, -100) -> bottom 50);
  // mirrored extent (x1 > x2) keeps its centre.
  mo::RawPort rot_port{.id = "q", .class_name = "Pin", .has_placement = true,
                       .x1 = -110, .y1 = -10, .x2 = -90, .y2 = 10, .rotation = 85.0};
  mo::RawPort mir_port{.id = "m", .class_name = "Pin", .has_placement = true,
                       .x1 = 110, .y1 = -10, .x2 = 90, .y2 = 10, .rotation = 0.0};

  // Three ports colliding on left @ 50 spread across free 5-unit slots and record moved distances.
  mo::RawPort c1{.id = "a", .class_name = "Pin", .has_placement = true, .x1 = -110, .y1 = -2, .x2 = -90, .y2 = 2};
  mo::RawPort c2{.id = "b", .class_name = "Pin", .has_placement = true, .x1 = -110, .y1 = -1, .x2 = -90, .y2 = 1};
  mo::RawPort c3{.id = "c", .class_name = "Pin", .has_placement = true, .x1 = -110, .y1 = 0, .x2 = -90, .y2 = 2};

  mo::ImportNotice notice;
  const auto ports = mo::quantise_ports({rot_port, mir_port, c1, c2, c3}, notice);
  REQUIRE(ports.size() == 5);
  CHECK(ports[0].side == "bottom");
  CHECK(ports[0].at == 50);
  CHECK(ports[1].side == "right");
  CHECK(ports[1].at == 50);

  std::set<int> left_slots;
  for (std::size_t i = 2; i < 5; ++i) {
    CHECK(ports[i].side == "left");
    CHECK(ports[i].at % 5 == 0);
    CHECK(ports[i].at >= 0);
    CHECK(ports[i].at <= 100);
    CHECK(left_slots.insert(ports[i].at).second);
  }
  CHECK(ports[2].at < ports[3].at);
  CHECK(ports[3].at < ports[4].at);
  CHECK(notice.moved.size() >= 2);
  for (const auto& m : notice.moved) {
    CHECK(m.distance > 0);
    CHECK(m.distance % 5 == 0);
  }
}

TEST_CASE("Icon(graphics) quantiser maps primitives, drops Bitmap/DynamicSelect, and bounds plates", "[modelica]") {
  // Wide aspect (>= 1.75:1) -> 2x1; 2x2 is never emitted even for large square extents.
  const auto wide = mo::parse_value(
      "{-175.0,-100.0,175.0,100.0,true,-,-,,{"
      "DynamicSelect(Rectangle(true,{0,0},0,{0,0,0},{255,255,255},LinePattern.Dash,FillPattern.HorizontalCylinder,0.5,"
      "BorderPattern.None,{{-100,50},{100,-50}},18.0), Rectangle(extent={{0,0},{10,10}})),"
      "Ellipse(true,{0,0},0,{160,160,160},{0,0,0},LinePattern.Solid,FillPattern.None,0.1,{{-40,40},{40,-40}},30.0,210.0),"
      "Polygon(true,{0,0},0,{0,0,0},{0,0,0},LinePattern.Solid,FillPattern.Solid,0.25,{{-20,10},{20,0},{-20,-10}},Smooth.None),"
      "Bitmap(extent={{-10,-10},{10,10}}, fileName=\"modelica://Pkg/img.png\"),"
      "Text(true,{0,0},0,{0,0,0},{0,0,0},LinePattern.Solid,FillPattern.None,0.25,{{-100,-60},{100,-90}},\"k=%k\",0.0,{-1,-1,-1},\"\",{},TextAlignment.Center)"
      "}}");
  mo::ImportNotice notice;
  const json icon = mo::quantise_icon({wide}, {}, notice);
  CHECK(icon.value("w", 1) == 2);
  CHECK(icon.value("h", 1) == 1);
  REQUIRE(icon["g"].size() == 4);
  CHECK(icon["g"][0]["t"] == "rect");
  CHECK(icon["g"][0]["dash"] == "axis");
  CHECK(icon["g"][0]["weight"] == "wire");
  CHECK(icon["g"][0]["fill"] == "raised");
  CHECK(icon["g"][0]["corner"] == "md");
  CHECK(icon["g"][1]["t"] == "arc");
  CHECK(icon["g"][1]["stroke"] == "muted");
  CHECK(icon["g"][1]["weight"] == "thin");
  CHECK(icon["g"][2]["t"] == "poly");
  CHECK(icon["g"][2]["fill"] == "ink");
  CHECK(icon["g"][3]["t"] == "text");
  CHECK(icon["g"][3]["s"] == "k={k}");
  CHECK(icon["g"][3]["font"] == "mono");

  CHECK(std::ranges::find(notice.dropped, "DynamicSelect") != notice.dropped.end());
  CHECK(std::ranges::find(notice.dropped, "Bitmap") != notice.dropped.end());
  CHECK(notice.simplified >= 3);

  // Large square box stays 1x1, never 2x2.
  mo::ImportNotice sq_notice;
  const json sq = mo::quantise_icon({mo::parse_value("{-400.0,-400.0,400.0,400.0,true,-,-,,{}}")}, {}, sq_notice);
  CHECK_FALSE(sq.contains("w"));
  CHECK_FALSE(sq.contains("h"));
}

TEST_CASE("live omc importer matches MSL goldens and resolves array, if, and fallback connectors", "[modelica][omc]") {
  if (std::system("command -v omc >/dev/null 2>&1") != 0) SKIP("omc not installed");
  mo::Omc omc;

  const std::pair<const char*, const char*> msl_cases[] = {
      {"Resistor", "Modelica.Electrical.Analog.Basic.Resistor"},
      {"Inductor", "Modelica.Electrical.Analog.Basic.Inductor"},
      {"Capacitor", "Modelica.Electrical.Analog.Basic.Capacitor"},
      {"Ground", "Modelica.Electrical.Analog.Basic.Ground"},
      {"Inertia", "Modelica.Mechanics.Rotational.Components.Inertia"},
  };
  for (const auto& [stem, cls] : msl_cases) {
    INFO(cls);
    const json live = mo::import_class(omc, cls).to_json();
    CHECK(live == read_golden(stem));
  }

  // Local fixture with Pin p[n] and `if useHeatPort`.
  const fs::path bus_mo = fs::path(PAPERBOAT_TEST_DATA) / "modelica" / "BusNode.mo";
  const auto bus = mo::import_model(omc, bus_mo);
  check_contract_tokens(bus.to_json());
  REQUIRE(bus.icon["ports"].size() == 3);
  CHECK(bus.icon["ports"][0]["id"] == "p");
  CHECK(bus.icon["ports"][0]["array"] == "n");
  CHECK(bus.icon["ports"][2]["id"] == "heatPort");
  CHECK(bus.icon["ports"][2]["if"] == "useHeatPort");

  // Unknown user-defined connector falls back to nearest domain handle and increments simplified.
  const fs::path tmp = fs::temp_directory_path() / "paperboat-modelica-custom-conn";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  std::ofstream(tmp / "CustomThermal.mo") << R"(package CustomThermal
  connector MyHeatNode
    Real T(unit = "K");
    flow Real Q_flow(unit = "W");
  end MyHeatNode;

  partial model BaseShell
    MyHeatNode port_a annotation(Placement(iconTransformation(extent = {{-110, -10}, {-90, 10}})));
  end BaseShell;

  model Wall
    extends BaseShell;
    MyHeatNode port_b annotation(Placement(iconTransformation(extent = {{90, -10}, {110, 10}})));
  end Wall;
end CustomThermal;
)";
  const auto wall = mo::import_model(omc, tmp / "CustomThermal.mo", "CustomThermal.Wall");
  check_contract_tokens(wall.to_json());
  REQUIRE(wall.connectors.contains("MyHeatNode"));
  CHECK(wall.connectors.at("MyHeatNode").handle == "diamond");
  CHECK(wall.connectors.at("MyHeatNode").dash == "dashed");
  CHECK(wall.notice.simplified >= 1);
  REQUIRE(wall.icon["ports"].size() == 2);
  CHECK(wall.icon["ports"][0]["id"] == "port_a");
  CHECK(wall.icon["ports"][1]["id"] == "port_b");
  fs::remove_all(tmp);
}

// --- squero/0 ops (#160 Wave 3) ---

namespace {

bool have_omc() { return std::system("command -v omc >/dev/null 2>&1") == 0; }

json op(mo::Omc& omc, json request, const mo::Session& session = {}) {
  return json::parse(mo::handle(omc, request.dump(), session));
}

}  // namespace

TEST_CASE("set_icon writes tokens as fixed Modelica values and refuses what it can't", "[squero]") {
  const json icon = {{"w", 2},
                     {"g", {{{"t", "rect"}, {"x", 15}, {"y", 35}, {"w", 70}, {"h", 30}, {"fill", "raised"}, {"corner", "md"}},
                            {{"t", "line"}, {"p", {{5, 50}, {15, 50}}}, {"weight", "wire"}, {"dash", "axis"}},
                            {{"t", "text"}, {"x", 50}, {"y", 50}, {"s", "R={R}"}, {"font", "mono"}, {"stroke", "muted"}}}}};
  const std::string text = mo::icon_annotation(icon);
  CHECK_THAT(text, Catch::Matchers::StartsWith("Icon(coordinateSystem(extent={{-200, -100}, {200, 100}})"));
  CHECK_THAT(text, Catch::Matchers::ContainsSubstring("fillPattern=FillPattern.Solid"));
  CHECK_THAT(text, Catch::Matchers::ContainsSubstring("pattern=LinePattern.Dash"));
  CHECK_THAT(text, Catch::Matchers::ContainsSubstring("textString=\"R=%R\""));
  // y runs down in an icon and up in Modelica: y=50 is the middle, y=0 the top.
  CHECK_THAT(mo::icon_annotation({{"g", {{{"t", "line"}, {"p", {{0, 0}, {100, 100}}}}}}}),
             Catch::Matchers::ContainsSubstring("points={{-100, 100}, {100, -100}}"));

  CHECK_THROWS_AS(mo::icon_annotation({{"g", {{{"t", "bezier"}}}}}), mo::OmcError);
  CHECK_THROWS_AS(mo::icon_annotation({{"g", {{{"t", "line"}, {"p", {{0, 0}, {5, 5}}}, {"stroke", "pink"}}}}}), mo::OmcError);
  CHECK_THROWS_AS(mo::icon_annotation({{"w", 2}, {"h", 2}, {"g", json::array()}}), mo::OmcError);

  mo::Omc omc;  // never started: both refuse before calling it
  CHECK(op(omc, {{"id", 1}, {"op", "set_icon"}, {"model", "Modelica.Electrical.Analog.Basic.Resistor"}, {"icon", icon}})["ok"] == false);
  CHECK(op(omc, {{"id", 2}, {"op", "save"}, {"model", "Modelica"}})["error"].get<std::string>().find("may edit") != std::string::npos);
  CHECK(op(omc, {{"id", 3}, {"op", "simulate"}, {"model", "M"}, {"stop", -1}})["ok"] == false);
}

TEST_CASE("set_icon round-trips the Wave 2 icon goldens through omc", "[squero][omc]") {
  if (!have_omc()) SKIP("omc not installed");
  mo::Omc omc;
  for (const char* stem : {"Resistor", "Inductor", "Capacitor", "Ground", "Inertia"}) {
    INFO(stem);
    const json golden = read_golden(stem)["icon"];
    const std::string model = std::string("RT") + stem;
    const fs::path file = fs::temp_directory_path() / (model + ".mo");
    std::ofstream(file) << "model " << model << "\nend " << model << ";\n";
    CHECK(op(omc, {{"id", 1}, {"op", "open"}}, {.file = file})["ok"] == true);
    const json reply = op(omc, {{"id", 2}, {"op", "set_icon"}, {"model", model}, {"icon", golden}});
    INFO(reply.dump());
    REQUIRE(reply["ok"] == true);
    const json back = mo::import_class(omc, model).icon;
    CHECK(back["g"] == golden["g"]);
    CHECK(back.value("w", 1) == golden.value("w", 1));
    CHECK(back.value("h", 1) == golden.value("h", 1));
    CHECK(op(omc, {{"id", 3}, {"op", "save"}, {"model", model}})["ok"] == true);
    std::ifstream saved(file);
    CHECK_THAT(std::string(std::istreambuf_iterator<char>(saved), {}), Catch::Matchers::ContainsSubstring("Icon("));
    fs::remove(file);
  }
}

TEST_CASE("icon, generate (dry, with progress) and simulate answer over handle", "[squero][omc]") {
  if (!have_omc()) SKIP("omc not installed");
  const fs::path dir = fs::temp_directory_path() / "paperboat-squero-test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path file = dir / "Decay.mo";
  std::ofstream(file) << R"(model Decay
  parameter Real k = 2;
  Real x(start = 1, fixed = true);
equation
  der(x) = -k * x;
end Decay;
)";
  mo::Omc omc;
  std::vector<json> pushed;
  const mo::Session session{[&](const std::string& text) { pushed.push_back(json::parse(text)); }, file};

  const json opened = op(omc, {{"id", 1}, {"op", "open"}}, session);
  REQUIRE(opened["ok"] == true);
  CHECK(opened["result"]["model"] == "Decay");

  const json icon = op(omc, {{"id", 2}, {"op", "icon"}, {"class", "Modelica.Electrical.Analog.Basic.Resistor"}}, session);
  REQUIRE(icon["ok"] == true);
  CHECK(icon["result"]["icon"]["ports"].size() == 3);  // p, n and the conditional heatPort
  CHECK(icon["result"]["connectors"].contains("PositivePin"));
  CHECK(op(omc, {{"id", 3}, {"op", "icon"}, {"class", "Modelica.Electrical.Analog.Basic.Resistor"}}, session) ==
        json{{"id", 3}, {"ok", true}, {"result", icon["result"]}});  // the cached answer is the same

  const json dry = op(omc, {{"id", 4}, {"op", "generate"}, {"source", file.string()}, {"model", "Decay"}, {"dry", true}}, session);
  INFO(dry.dump());
  REQUIRE(dry["ok"] == true);
  CHECK(dry["result"]["path"] == "ode");
  CHECK(dry["result"]["files"].contains("domain/decay.cpp"));
  CHECK_FALSE(fs::exists("decay-cpp"));
  CHECK_FALSE(fs::exists(dir / "decay-cpp"));
  REQUIRE_FALSE(pushed.empty());
  CHECK(pushed.front()["op"] == "progress");
  CHECK(pushed.front()["id"] == 4);
  CHECK(pushed.back()["stage"] == "generate");
  CHECK(pushed.back()["fraction"] == 1);

  const json sim = op(omc, {{"id", 5}, {"op", "simulate"}, {"model", "Decay"}, {"stop", 1.0}, {"vars", {"x"}}}, session);
  INFO(sim.dump());
  REQUIRE(sim["ok"] == true);
  const auto& t = sim["result"]["t"];
  const auto& x = sim["result"]["series"]["x"];
  REQUIRE(t.size() == x.size());
  CHECK(t.size() > 100);
  CHECK(x.front().get<double>() == Catch::Approx(1.0));
  CHECK(x.back().get<double>() == Catch::Approx(std::exp(-2.0)).epsilon(0.01));  // x' = -2x
  CHECK(sim["result"]["series"].size() == 1);

  fs::remove_all(dir);
}
