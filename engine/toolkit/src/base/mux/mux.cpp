// base/mux: base/mux and base/demux (#164): numbers into one vector and back. Their ports are the

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common.hpp"
#include "base/conversion_shared.hpp"

namespace paperboat::toolkit::base {

void add_mux(Registry& registry) {
  // base/mux and base/demux (#164): numbers into one vector and back. Their ports are the
  // `names` ("x v theta"); `units` ("m m/s rad") gives each a unit, by position.
  const std::vector<ParamDef> named = {{.id = "names", .type = "text", .default_text = "a b"},
                                       {.id = "units", .type = "text", .default_text = ""}};
  Template mux;
  mux.name = "base/mux";
  mux.def.type = "mux";
  mux.def.label = "Mux";
  mux.def.inputs = {{.id = "a"}, {.id = "b"}};
  mux.def.outputs = {vector_port("y", {{.id = "a"}, {.id = "b"}})};
  mux.def.params = named;
  mux.def.ports_from = "names";
  mux.def.description = "Joins numbers into one vector, one input per name.";
  mux.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "a b", {"y"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      Vector y;
      y.reserve(names.size());
      for (const auto& n : names) y.push_back(get_or(in, n));
      return Values{{"y", std::move(y)}};
    });
  };
  registry.add(std::move(mux));
}

}  // namespace paperboat::toolkit::base
