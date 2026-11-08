// base/xor.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_xor(Registry& registry) {
  add_test(registry, "xor", "Xor", {"a", "b"}, {},
           [](const Values& in, const Values&) { return holds(in, "a") ^ holds(in, "b"); },
           "Holds when exactly one of a or b holds.", {.compact = true, .glyph = "=1"});
}

}  // namespace paperboat::toolkit::base
