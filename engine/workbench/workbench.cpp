#include "workbench.hpp"

#include <string>
#include <string_view>

namespace paperboat::workbench {
namespace {

// A starter of Sine into a Graph: the workbench runs as soon as it opens. The Station page
// is hidden by default (the workbench has no objects), and control is optional: it comes
// from the external control toolkit, so %CONTROL% below becomes its import only when that
// is installed.
constexpr std::string_view kDescriptor = R"(# The workbench: Flow and Dashboard with the standard toolkits, no domain.
domain = "workbench"
import = ["base", "math", "signal", "dashboard"%CONTROL%]

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

// The marker in kDescriptor, replaced with the control import when that toolkit is built in.
constexpr std::string_view kControlMarker = "%CONTROL%";
constexpr std::string_view kControlImport = ", \"control\"";

}  // namespace

const toolkit::Catalog& catalog() {
  static const toolkit::Catalog catalog = [] {
    std::string descriptor(kDescriptor);
    const auto at = descriptor.find(kControlMarker);
    descriptor.replace(at, kControlMarker.size(),
                       toolkit::builtins().package("control").empty() ? "" : kControlImport);
    return toolkit::load_descriptor(descriptor);
  }();
  return catalog;
}

const core::Backend& backend() {
  static const core::Backend backend = {.engine = "workbench", .catalog = &catalog()};
  return backend;
}

}  // namespace paperboat::workbench
