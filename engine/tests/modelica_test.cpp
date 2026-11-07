// engine/modelica (#160): the omc bridge's allow-list and answer parser, the expression
// translator, and generate()'s path choice against a real omc.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

#include "paperboat/modelica/generate.hpp"
#include "paperboat/modelica/omc.hpp"
#include "paperboat/modelica/squero.hpp"

namespace mo = paperboat::modelica;
namespace fs = std::filesystem;

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
  CHECK(fs::is_regular_file(dir / "decay-cpp" / "descriptor.toml"));
  CHECK(fs::is_regular_file(dir / "decay-cpp" / "modelica" / "Decay.mo"));

  CHECK_THROWS_WITH(mo::generate(omc, {.source = dir / "Bounce.mo", .out = dir / "bounce-cpp", .path = "ode"}),
                    Catch::Matchers::ContainsSubstring("events"));
  CHECK(mo::check(omc, dir / "Bounce.mo", "Bounce").ok);
  fs::remove_all(dir);
}
