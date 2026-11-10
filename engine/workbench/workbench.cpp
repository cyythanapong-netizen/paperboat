#include "workbench.hpp"

#include <string>
#include <string_view>
#include <vector>

#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
#include "paperboat/toolkit/optimization.hpp"
#endif
#ifdef PAPERBOAT_HAS_CONTROL
#include "paperboat/toolkit/control.hpp"
#endif
#ifdef PAPERBOAT_HAS_MESHOP
#include "paperboat/toolkit/meshop.hpp"
#endif

namespace paperboat::workbench {
namespace {

// A starter of Sine into a Graph: the workbench runs as soon as it opens. The Station page
// is hidden by default (the workbench has no objects), and installed toolkits are loaded into
// its catalog so their blocks appear on the palette.
constexpr std::string_view kDescriptorPrefix = R"(# The workbench: Flow and Dashboard with the standard and installed toolkits, no domain.
domain = "workbench"
import = [)";

constexpr std::string_view kDescriptorSuffix = R"(]

[station]
view = "graph"
visible = false

[graph]
nodes = [
  { id = "sine", type = "sine", pos = [0, 0] },
  { id = "graph", type = "graph", pos = [280, 0] },
]
edges = [{ from = ["sine", "y"], to = ["graph", "in"] }]
)";

toolkit::Registry make_registry() {
  toolkit::Registry reg = toolkit::builtins();
#ifdef PAPERBOAT_HAS_OPENCV
  toolkit::opencv::register_opencv(reg);
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
  toolkit::optimization::register_optimization(reg);
#endif
#ifdef PAPERBOAT_HAS_CONTROL
  toolkit::control::register_control(reg);
#endif
#ifdef PAPERBOAT_HAS_MESHOP
  toolkit::meshop::register_meshop(reg);
#endif
  return reg;
}

}  // namespace

const toolkit::Registry& workbench_registry() {
  static const toolkit::Registry reg = make_registry();
  return reg;
}

const toolkit::Catalog& catalog() {
  static const toolkit::Catalog catalog = [] {
    const auto& registry = workbench_registry();
    std::string imports;
    for (const auto& pkg : registry.packages()) {
      if (!imports.empty()) imports += ", ";
      imports += "\"" + pkg + "\"";
    }
    std::string descriptor = std::string(kDescriptorPrefix) + imports + std::string(kDescriptorSuffix);
    return toolkit::load_descriptor(descriptor, registry);
  }();
  return catalog;
}

const core::Backend& backend() {
  static const core::Backend backend = {.engine = "workbench", .catalog = &catalog()};
  return backend;
}

}  // namespace paperboat::workbench
