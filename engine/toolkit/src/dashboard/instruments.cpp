// The dashboard package: monitors, the blocks that show a flow's values on the Dashboard.
// Each declares its instrument; the core sends what it shows as `signal` events. A
// monitor that needs to turn its input into something showable (an image into a picture)
// returns it as output "value" (see opencv/view); otherwise the core sends input "in".

#include "common.hpp"

namespace paperboat::toolkit::dashboard {
namespace {

struct Passive final : Block {
  Values step(const Context&, const Values&) override { return {}; }
};

Template monitor(std::string type, std::string label, std::string instrument, std::string description) {
  Template t;
  t.name = "dashboard/" + type;
  t.def = {.type = std::move(type), .label = std::move(label), .kind = Kind::Monitor, .inputs = {{"in", "any"}},
           .outputs = {}, .params = {}, .instrument = std::move(instrument), .description = std::move(description)};
  t.make = [](const Values&, const Config&) { return std::make_unique<Passive>(); };
  return t;
}

}  // namespace

void add_instruments(Registry& registry) {
  registry.add(monitor("graph", "Graph", "graph", "Plots its input over time on the Dashboard."));
  registry.add(monitor("display", "Display", "display", "Shows its latest value on the Dashboard."));
}

}  // namespace paperboat::toolkit::dashboard
