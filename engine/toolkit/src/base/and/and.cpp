// base/and.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_and(Registry& registry) {
  add_test(
      registry, "and", "And", {"a", "b"},
      {{.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& in, const Values& p) {
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 1, 26);
        for (int i = 0; i < n; ++i) {
          if (!holds(in, std::string(1, static_cast<char>('a' + i)))) return false;
        }
        return true;
      },
      "Holds when all of its inputs hold.", {.compact = true, .glyph = "&"}, "inputs");
}

}  // namespace paperboat::toolkit::base
