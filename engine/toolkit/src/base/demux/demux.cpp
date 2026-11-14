// base/demux.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common.hpp"
#include "base/conversion_shared.hpp"

namespace paperboat::toolkit::base {

void add_demux(Registry& registry) {
  const std::vector<ParamDef> named = {{.id = "names", .type = "text", .default_text = "a b"},
                                       {.id = "units", .type = "text", .default_text = ""}};
  Template demux;
  demux.name = "base/demux";
  demux.def.type = "demux";
  demux.def.label = "Demux";
  demux.def.inputs = {vector_port("u", {{.id = "a"}, {.id = "b"}})};
  demux.def.outputs = {{.id = "a"}, {.id = "b"}};
  demux.def.params = named;
  demux.def.outputs_from = "names";
  demux.def.description = "Splits a vector into numbers, one output per name.";
  demux.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "a b", {"u"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      const Vector u = as_vector(in, "u");  // unwired: zeros
      Values out;
      for (std::size_t i = 0; i < names.size(); ++i) out[names[i]] = i < u.size() ? u[i] : 0.0;
      return out;
    });
  };
  demux.def.view = {.compact = true, .value = "{names}", .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"line","p":[[10,50],[45,50]],"weight":"wire"},{"t":"line","p":[[10,40],[45,40]],"weight":"thin"},{"t":"line","p":[[45,50],[65,25],[90,25]]},{"t":"line","p":[[45,50],[90,50]]},{"t":"line","p":[[45,50],[65,75],[90,75]]}]})")};  // one vector fanning out
  registry.add(std::move(demux));
}

}  // namespace paperboat::toolkit::base
