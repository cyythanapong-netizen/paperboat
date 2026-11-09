// list/list_length.

#include "common.hpp"
#include "list/list_shared.hpp"

namespace paperboat::toolkit::list {

void add_list_length(Registry& registry) {
  Template t;
  t.name = "list/list_length";
  t.def = magic_def("list_length", "List Length", list_forms({}, {{"n"}}, {{"n"}}));
  t.def.description = "How many items its list has (numbers, or points in the points form).";
  t.def.view = {.compact = true, .glyph = "#[]"};
  t.make = [](const Values&, const Config& config) {
    const bool points = points_form(config);
    return std::make_unique<Stateless>([points](const Context&, const Values& in) {
      const double n = static_cast<double>(points ? points_at(in, "list").size() : as_vector(in, "list").size());
      return Values{{"n", n}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
