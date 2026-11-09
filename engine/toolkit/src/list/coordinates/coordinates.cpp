// list/coordinates.

#include "common.hpp"
#include "list/list_shared.hpp"

namespace paperboat::toolkit::list {

void add_coordinates(Registry& registry) {
  // A list of points split into one list per coordinate, as Demux splits a vector.
  Template t;
  t.name = "list/coordinates";
  t.def.type = "coordinates";
  t.def.label = "Coordinates";
  t.def.inputs = {points_port("points", {{.id = "x"}, {.id = "y"}})};
  t.def.outputs = {numbers_port("x"), numbers_port("y")};
  t.def.params = {{.id = "names", .type = "text", .default_text = "x y"}, {.id = "units", .type = "text", .default_text = ""}};
  t.def.outputs_from = "names";
  t.def.description = "Splits a list of points into lists of coordinates, one output per name.";
  t.def.view = {.compact = true, .glyph = "x y"};
  t.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "x y", {"points"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      const Table points = points_at(in, "points");  // its port checks each point has one coordinate per name
      Values out;
      for (std::size_t c = 0; c < names.size(); ++c) {
        Vector list;
        list.reserve(points.size());
        for (const auto& p : points) list.push_back(c < p.size() ? p[c] : 0.0);
        out[names[c]] = std::move(list);
      }
      return out;
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
