// dashboard/graph.

#include "common.hpp"
#include "dashboard/instruments_shared.hpp"

namespace paperboat::toolkit::dashboard {

void add_graph(Registry& registry) {
  registry.add(monitor("graph", "Graph", "graph", "Plots its input over time on the Dashboard."));
}

}  // namespace paperboat::toolkit::dashboard
