// dashboard/view3d: the Board's 3D view of the Station.

#include "common.hpp"
#include "dashboard/instruments_shared.hpp"

namespace paperboat::toolkit::dashboard {

void add_view3d(Registry& registry) {
  Template t;
  t.name = "dashboard/view3d";
  t.def = {
      .type = "view3d",
      .label = "3D view",
      .kind = Kind::Monitor,
      .inputs = {},
      .outputs = {},
      .params = {{.id = "grid", .default_value = 1.0, .type = "boolean"}},
      .instrument = "view3d",
      .description = "Shows the Station's 3D scene — its objects and meshes — on the Board.",
      .view = {.compact = true, .glyph = "3D"},
  };
  t.make = [](const Values&, const Config&) { return std::make_unique<Passive>(); };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::dashboard
