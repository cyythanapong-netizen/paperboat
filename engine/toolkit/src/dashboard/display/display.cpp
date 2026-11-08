// dashboard/display.

#include "common.hpp"
#include "dashboard/instruments_shared.hpp"

namespace paperboat::toolkit::dashboard {

void add_display(Registry& registry) {
  auto display = monitor("display", "Display", "display", "Shows its latest value on the Dashboard.");
  display.def.whole_vector = true;  // a readout shows the whole vector
  registry.add(std::move(display));
}

}  // namespace paperboat::toolkit::dashboard
