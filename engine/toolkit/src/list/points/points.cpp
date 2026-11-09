// list/points.

#include "common.hpp"
#include "list/list_shared.hpp"
#include "list/points/icon.hpp"

namespace paperboat::toolkit::list {

void add_points(Registry& registry) {
  // Lists of coordinates into one list of points, as Mux joins numbers into a vector: one
  // input per name ("x z"), units by position.
  Template t;
  t.name = "list/points";
  t.def.type = "points";
  t.def.label = "Points";
  t.def.inputs = {numbers_port("x"), numbers_port("y")};
  t.def.outputs = {points_port("points", {{.id = "x"}, {.id = "y"}})};
  t.def.params = {{.id = "names", .type = "text", .default_text = "x y"}, {.id = "units", .type = "text", .default_text = ""}};
  t.def.ports_from = "names";
  t.def.description = "Joins lists of coordinates into a list of points, one input per name.";
  t.def.view = {.compact = true, .icon = points_icon()};
  t.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "x y", {"points"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      std::size_t n = 0;
      std::string first;
      for (const auto& name : names) {
        if (!in.contains(name)) continue;
        const std::size_t size = as_vector(in, name).size();
        if (first.empty()) {
          n = size;
          first = name;
        } else if (size != n) {
          throw std::invalid_argument("input " + name + " has " + std::to_string(size) + " items, but " + first + " has " +
                                      std::to_string(n) + ": the lists into Points are one length");
        }
      }
      Table points(n, std::vector<double>(names.size(), 0.0));  // an unwired coordinate is 0
      for (std::size_t c = 0; c < names.size(); ++c) {
        const Vector list = as_vector(in, names[c]);
        for (std::size_t i = 0; i < list.size() && i < n; ++i) points[i][c] = list[i];
      }
      return Values{{"points", std::move(points)}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
