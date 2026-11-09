// list/range.

#include "common.hpp"
#include "list/list_shared.hpp"
#include "list/range/icon.hpp"

namespace paperboat::toolkit::list {

void add_range(Registry& registry) {
  Template t;
  t.name = "list/range";
  t.def.type = "range";
  t.def.label = "Range";
  t.def.kind = Kind::Source;
  t.def.outputs = {numbers_port("y")};
  t.def.params = {{.id = "start", .default_value = 0.0}, {.id = "stop", .default_value = 1.0}, {.id = "count", .default_value = 11.0}};
  t.def.description = "A list of count numbers evenly spaced from start to stop, both included.";
  t.def.view = {.compact = true, .value = "{start}…{stop}", .icon = range_icon()};
  t.make = [](const Values& p, const Config&) {
    const double start = get_or(p, "start"), stop = get_or(p, "stop");
    const std::size_t n = count_param(p, "count");
    Vector y(n);
    for (std::size_t i = 0; i < n; ++i)
      y[i] = n == 1 ? start : start + (stop - start) * static_cast<double>(i) / static_cast<double>(n - 1);
    if (n > 1) y.back() = stop;  // exactly, whatever the rounding on the way
    return std::make_unique<Stateless>([y = std::move(y)](const Context&, const Values&) { return Values{{"y", y}}; });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
