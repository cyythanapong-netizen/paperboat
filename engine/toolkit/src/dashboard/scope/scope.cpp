// dashboard/scope (#37)

#include "common.hpp"
#include "dashboard/instruments_shared.hpp"

namespace paperboat::toolkit::dashboard {

void add_scope(Registry& registry) {
  // A Scope (#37): several inputs (a, b… by its `inputs` count), each a channel; a vector
  // input is one channel per component. How it draws is the Board layout's, not params.
  auto scope = monitor("scope", "Scope", "scope", "Plots several signals over time to study: axes, zoom, cursors, triggers.");
  // A Scope plots numbers: a signal, or a whole vector as one channel per component. A
  // `broadcast` signal port is how a signal port also accepts a vector wire (block.hpp).
  scope.def.inputs = {{.id = "a", .type = "signal", .broadcast = true}, {.id = "b", .type = "signal", .broadcast = true}};
  scope.def.params = {{.id = "inputs", .default_value = 2.0}};
  scope.def.ports_from = "inputs";
  scope.def.whole_vector = true;       // a vector is one sample: a channel per component
  scope.def.sample_per_input = true;  // one sample per wired input, tagged with it
  registry.add(std::move(scope));
}

}  // namespace paperboat::toolkit::dashboard
