// list/aggregate.

#include <algorithm>
#include <limits>

#include "common.hpp"
#include "list/list_shared.hpp"

namespace paperboat::toolkit::list {

namespace {

enum class Op { Sum, Mean, Min, Max };

// `op` over the numbers `v`: the sum of none is 0; the others of none are an error.
double reduce(Op op, const Vector& v, const char* name) {
  if (v.empty() && op != Op::Sum) throw std::invalid_argument(std::string("the ") + name + " of an empty list is not defined");
  double out = op == Op::Min ? std::numeric_limits<double>::infinity()
               : op == Op::Max ? -std::numeric_limits<double>::infinity()
                               : 0.0;
  for (const double x : v) {
    if (op == Op::Min) out = std::min(out, x);
    else if (op == Op::Max) out = std::max(out, x);
    else out += x;
  }
  return op == Op::Mean ? out / static_cast<double>(v.size()) : out;
}

}  // namespace

void add_aggregate(Registry& registry) {
  Template t;
  t.name = "list/aggregate";
  t.def = magic_def("aggregate", "Aggregate", list_forms({}, {{"y"}}, {vector_port("y")}),
                    {{.id = "op", .type = "enum", .default_text = "sum", .options = {"sum", "mean", "min", "max"}}});
  t.def.description = "One number from its list: the sum, mean, min or max. Of points, one per coordinate "
                      "(the mean is the centroid, min and max the corners of the bounding box).";
  t.def.view = {.compact = true, .glyph = "{op}", .value = "{op}"};
  t.make = [](const Values&, const Config& config) {
    const bool points = points_form(config);
    const std::string name = config.contains("op") ? config.at("op") : "sum";
    const Op op = name == "mean" ? Op::Mean : name == "min" ? Op::Min : name == "max" ? Op::Max : Op::Sum;
    return std::make_unique<Stateless>([points, op, name](const Context&, const Values& in) {
      if (!points) return Values{{"y", reduce(op, as_vector(in, "list"), name.c_str())}};
      const Table list = points_at(in, "list");
      if (list.empty() && op != Op::Sum)
        throw std::invalid_argument("the " + name + " of an empty list is not defined");
      const std::size_t d = list.empty() ? 0 : list.front().size();
      Vector y(d);
      for (std::size_t c = 0; c < d; ++c) {
        Vector column;
        column.reserve(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) {
          if (list[i].size() != d)
            throw std::invalid_argument("point " + std::to_string(i) + " has " + std::to_string(list[i].size()) +
                                        " coordinates, but point 0 has " + std::to_string(d));
          column.push_back(list[i][c]);
        }
        y[c] = reduce(op, column, name.c_str());
      }
      return Values{{"y", std::move(y)}};
    });
  };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::list
