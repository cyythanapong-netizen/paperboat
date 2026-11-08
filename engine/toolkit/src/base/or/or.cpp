// base/or.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {

void add_or(Registry& registry) {
  add_test(
      registry, "or", "Or", {"a", "b"},
      {{.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& in, const Values& p) {
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 1, 26);
        for (int i = 0; i < n; ++i) {
          if (holds(in, std::string(1, static_cast<char>('a' + i)))) return true;
        }
        return false;
      },
      "Holds when any of its inputs holds.", {.compact = true, .glyph = "≥1"}, "inputs");
}

}  // namespace paperboat::toolkit::base
