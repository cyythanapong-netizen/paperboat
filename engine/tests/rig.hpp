#pragma once

// Test fixture: a tiny domain of the tests' own, "rig", so the engine's tests need nothing
// from the examples. One station object, a slider: a mass on a rail, pushed by a force
// (clamped to max_force). Its linked block takes force and gives x and v.
// data/rig.toml is its descriptor: a PD loop that brings the slider back to x = 0.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/registry.hpp"
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

inline const tk::Registry& registry() {
  static const tk::Registry r = [] {
    tk::Registry out = tk::builtins();
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
