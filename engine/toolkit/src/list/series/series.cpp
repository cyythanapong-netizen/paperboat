// list/series.

#include "common.hpp"
#include "list/list_shared.hpp"

namespace paperboat::toolkit::list {

void add_series(Registry& registry) {
  Template t;
  t.name = "list/series";
  t.def.type = "series";
  t.def.label = "Series";
  t.def.kind = Kind::Source;
  t.def.outputs = {numbers_port("y")};
  t.def.params = {{.id = "start", .default_value = 0.0}, {.id = "step", .default_value = 1.0}, {.id = "count", .default_value = 10.0}};
  t.def.description = "A list of count numbers: start, then each one step more.";
  t.def.view = {.compact = true, .glyph = "1 2 3", .value = "{start} +{step}"};
  t.make = [](const Values& p, const Config&) {
    const double start = get_or(p, "start"), step = get_or(p, "step");
    const std::size_t n = count_param(p, "count");
    Vector y(n);
    for (std::size_t i = 0; i < n; ++i) y[i] = start + step * static_cast<double>(i);
    return std::make_unique<Stateless>([y = std::move(y)](const Context&, const Values&) { return Values{{"y", y}}; });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
