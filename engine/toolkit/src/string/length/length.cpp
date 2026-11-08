// string/length.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::string {

void add_length(Registry& registry) {
  // string/length
  Template len;
  len.name = "string/length";
  len.def.type = "length";
  len.def.label = "Length";
  len.def.kind = Kind::Function;
  len.def.inputs = {{.id = "text", .type = "text"}};
  len.def.outputs = {{.id = "y", .type = "signal"}};
  len.def.description = "Returns the character count of text.";
  len.def.view = {.compact = true, .glyph = "#T"};
  len.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
      return Values{{"y", static_cast<double>(text.size())}};
    });
  };
  registry.add(std::move(len));
}

}  // namespace paperboat::toolkit::string
