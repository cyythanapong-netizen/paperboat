// list/item.

#include "common.hpp"
#include "list/list_shared.hpp"

namespace paperboat::toolkit::list {

void add_item(Registry& registry) {
  Template t;
  t.name = "list/item";
  t.def = magic_def("item", "Item", list_forms({{"i"}}, {{"y"}}, {vector_port("y")}),
                    {{.id = "index", .default_value = 0.0}});
  t.def.description = "The item of its list at index (0 the first, -1 the last); input i, when wired, is the index.";
  t.def.view = {.compact = true, .glyph = "[i]", .value = "[{index}]"};
  t.make = [](const Values& p, const Config& config) {
    const bool points = points_form(config);
    const double index = get_or(p, "index");
    return std::make_unique<Stateless>([points, index](const Context&, const Values& in) {
      const double i = in.contains("i") ? get_or(in, "i") : index;
      if (points) {
        const Table list = points_at(in, "list");
        return Values{{"y", list[at_index(i, list.size())]}};
      }
      const Vector list = as_vector(in, "list");
      return Values{{"y", list[at_index(i, list.size())]}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
