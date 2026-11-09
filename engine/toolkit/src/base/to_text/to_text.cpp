// base/to_text.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common.hpp"
#include "base/conversion_shared.hpp"

namespace paperboat::toolkit::base {
namespace {

class ToText final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    auto it = in.find("in");
    if (it == in.end()) return {{"y", std::string("")}};
    return {{"y", value_to_text(it->second)}};
  }
};

}  // namespace

void add_to_text(Registry& registry) {
  // base/to_text
  Template to_txt;
  to_txt.name = "base/to_text";
  to_txt.def.type = "to_text";
  to_txt.def.label = "To Text";
  to_txt.def.kind = Kind::Function;
  to_txt.def.inputs = {{.id = "in", .type = "any"}};
  to_txt.def.outputs = {{.id = "y", .type = "text"}};
  to_txt.def.description = "Converts numbers, booleans, or lists to formatted text.";
  to_txt.def.view = {.compact = true, .glyph = "→T"};
  to_txt.make = [](const Values&, const Config&) { return std::make_unique<ToText>(); };
  to_txt.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(to_txt));
}

}  // namespace paperboat::toolkit::base
