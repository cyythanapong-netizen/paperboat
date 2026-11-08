// base/compare.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_compare(Registry& registry) {
  add_test(registry, "compare", "Compare", {"a", "b"},
           {{.id = "op", .type = "enum", .default_text = ">", .options = {">", ">=", "<", "<=", "==", "!="}}},
           [](const Values& in, const Values& p) {
             const double a = get_or(in, "a"), b = get_or(in, "b");
             switch (static_cast<int>(get_or(p, "op"))) {  // the op's index in its options
               case 0: return a > b;
               case 1: return a >= b;
               case 2: return a < b;
               case 3: return a <= b;
               case 4: return a == b;
               default: return a != b;
             }
           }, "Compares a with b: greater, less, equal…", {.compact = true, .glyph = "cmp", .value = "{op}"});
}

}  // namespace paperboat::toolkit::base
