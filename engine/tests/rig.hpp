#pragma once

// Test fixture: a tiny domain of the tests' own, "rig", so the engine's tests need nothing
// from the examples. One station object, a slider: a mass on a rail, pushed by a force
// (clamped to max_force). Its linked block takes force and gives x and v.
// data/rig.toml is its descriptor: a PD loop that brings the slider back to x = 0.

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/registry.hpp"
#include "paperboat/toolkit/solve.hpp"
#include "paperboat/toolkit/station.hpp"

namespace rig {

namespace tk = paperboat::toolkit;

struct Slider final : tk::StationObject {
  explicit Slider(const tk::Values& p)
      : mass(tk::get_or(p, "mass")), max_force(tk::get_or(p, "max_force")), x(tk::get_or(p, "x0")) {}
  tk::Values read() const override { return {{"x", x}, {"v", v}}; }
  void step(const tk::Context& ctx, const tk::Values& in) override {  // semi-implicit Euler
    v += std::clamp(tk::get_or(in, "force"), -max_force, max_force) / mass * ctx.dt;
    x += v * ctx.dt;
  }
  std::vector<tk::Entity> entities(const std::string& id, double z) const override {
    return {{.id = id + "/rail", .shape = "box", .pos = {0, -0.1, z}, .size = {8, 0.05, 0.3}},
            {.id = id + "/body", .shape = "box", .pos = {x, 0, z}, .size = {0.5, 0.3, 0.4}}};
  }
  double mass, max_force, x, v = 0;
};


// The control blocks the engine used to ship built-in: the rig's own copies, so the engine's
// tests still exercise a clock loop, a delay and a tunable PID without the optional toolkit.
struct TestPid final : tk::Block {
  TestPid(const tk::Values& p, const tk::Config&)
      : kp(tk::get_or(p, "kp", 1)), ki(tk::get_or(p, "ki")), kd(tk::get_or(p, "kd")) {}
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    const double e = tk::get_or(in, "e");
    integral += e * ctx.dt;
    const double derivative = (prev && ctx.dt > 0) ? (e - *prev) / ctx.dt : 0.0;
    prev = e;
    return {{"u", kp * e + ki * integral + kd * derivative}};
  }
  bool set_param(std::string_view id, double value) override {
    double* gain = id == "kp" ? &kp : id == "ki" ? &ki : id == "kd" ? &kd : nullptr;
    if (gain) *gain = value;
    return gain != nullptr;
  }
  double kp, ki, kd;
  double integral = 0.0;
  std::optional<double> prev;
};

struct TestIntegrator final : tk::Block {
  TestIntegrator(const tk::Values& p, const tk::Config& c)
      : y(tk::get_or(p, "initial")), solver(tk::solve::make(c.contains("solver") ? c.at("solver") : "auto")) {}
  tk::Values outputs() const override { return {{"y", y}}; }
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    tk::solve::State s = {y};
    solver->step(s, ctx.t, ctx.dt, [&](double, const tk::solve::State&) { return tk::solve::State{tk::get_or(in, "u")}; });
    y = s[0];
    return outputs();
  }
  double y;
  std::unique_ptr<tk::solve::Solver> solver;
};

struct TestLag final : tk::Block {
  TestLag(const tk::Values& p, const tk::Config& c)
      : tau(tk::get_or(p, "tau", 1)), solver(tk::solve::make(c.contains("solver") ? c.at("solver") : "auto")) {}
  tk::Values outputs() const override { return {{"y", y}}; }
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    const double u = tk::get_or(in, "u");
    if (tau <= 0) {
      y = u;
    } else {
      tk::solve::State s = {y};
      solver->step(s, ctx.t, ctx.dt, [&](double, const tk::solve::State& x) { return tk::solve::State{(u - x[0]) / tau}; });
      y = s[0];
    }
    return outputs();
  }
  double tau;
  double y = 0.0;
  std::unique_ptr<tk::solve::Solver> solver;
};

struct TestDelay final : tk::Block {
  TestDelay(const tk::Values& p, const tk::Config&)
      : n(std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(tk::get_or(p, "steps", 1))))),
        initial(tk::get_or(p, "initial")) {}
  tk::Values outputs() const override {
    if (past.size() < n) return {{"y", initial}};
    return {{"y", past.front()}};
  }
  tk::Values step(const tk::Context&, const tk::Values& in) override {
    const auto it = in.find("u");
    past.push_back(it == in.end() ? tk::Value{0.0} : it->second);
    if (past.size() > n) past.pop_front();
    return {};
  }
  std::size_t n;
  double initial;
  std::deque<tk::Value> past;
};


struct Pd final : tk::Block {
  explicit Pd(const tk::Values& p) : kp(tk::get_or(p, "kp", 4)), kd(tk::get_or(p, "kd", 3)) {}
  tk::Values step(const tk::Context& ctx, const tk::Values& in) override {
    const double e = tk::get_or(in, "e");
    const double derivative = (prev && ctx.dt > 0) ? (e - *prev) / ctx.dt : 0.0;
    prev = e;
    return {{"u", kp * e + kd * derivative}};
  }
  double kp, kd;
  std::optional<double> prev;
};

inline const tk::Registry& registry() {
  static const tk::Registry r = [] {
    tk::Registry out = tk::builtins();
    out.add(tk::define_block<Pd>("rig", {.type = "pd", .label = "PD", .inputs = {{"e"}}, .outputs = {{"u"}},
                                         .params = {{.id = "kp", .default_value = 4}, {.id = "kd", .default_value = 3}}}));
    auto pid = tk::define_block<TestPid>("control", {.type = "pid", .label = "PID", .inputs = {{"e"}}, .outputs = {{"u"}},
                                                     .params = {{.id = "kp", .default_value = 1}, {.id = "ki", .default_value = 0}, {.id = "kd", .default_value = 0}}});
    pid.def.elementwise = true;
    out.add(std::move(pid));
    auto integ = tk::define_block<TestIntegrator>("control", {.type = "integrator", .label = "Integrator", .inputs = {{"u"}}, .outputs = {{"y"}},
                                                             .params = {{.id = "initial", .default_value = 0}, {.id = "solver", .type = "text", .default_text = "auto"}}});
    integ.def.feedthrough = false;
    integ.def.elementwise = true;
    out.add(std::move(integ));
    auto lag = tk::define_block<TestLag>("control", {.type = "lag", .label = "Lag", .inputs = {{"u"}}, .outputs = {{"y"}},
                                                     .params = {{.id = "tau", .default_value = 1}, {.id = "solver", .type = "text", .default_text = "auto"}}});
    lag.def.feedthrough = false;
    lag.def.elementwise = true;
    out.add(std::move(lag));
    auto delay = tk::define_block<TestDelay>("control", {.type = "delay", .label = "Delay", .inputs = {{"u", "any"}}, .outputs = {{"y", "any"}},
                                                         .params = {{.id = "steps", .default_value = 1}, {.id = "initial", .default_value = 0}}});
    delay.def.feedthrough = false;
    out.add(std::move(delay));
    out.add(tk::define_object<Slider>(
        "rig", {.type = "slider",
                .label = "Slider",
                .role = "body",
                .props = {{.id = "mass", .default_value = 1}, {.id = "max_force", .default_value = 50}, {.id = "x0", .default_value = 0}},
                .links = {},
                .block = tk::BlockDef{.type = "slider", .label = "Slider", .kind = tk::Kind::Source,
                                      .inputs = {{"force"}}, .outputs = {{"x"}, {"v"}}}}));
    return out;
  }();
  return r;
}

inline const tk::Catalog& catalog() {
  static const tk::Catalog c = tk::load_descriptor_file(PAPERBOAT_TEST_DATA "/rig.toml", registry());
  return c;
}

}  // namespace rig
