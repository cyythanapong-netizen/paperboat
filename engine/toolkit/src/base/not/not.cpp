// base/not.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_not(Registry& registry) {
  add_test(registry, "not", "Not", {"u"}, {}, [](const Values& in, const Values&) { return !holds(in, "u"); },
           "Holds when its input doesn't.", {.compact = true, .glyph = "¬"});
}

}  // namespace paperboat::toolkit::base
