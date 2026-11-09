// string/trim.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::string {
namespace {

class Trim final : public Block {
 public:
  explicit Trim(int mode) : mode_(mode) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string_view sv = text;
    if (mode_ == 0 || mode_ == 1) {
      while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    }
    if (mode_ == 0 || mode_ == 2) {
      while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    }
    return {{"y", std::string(sv)}};
  }

 private:
  int mode_;
};

}  // namespace

void add_trim(Registry& registry) {
  // string/trim
  Template trim;
  trim.name = "string/trim";
  trim.def.type = "trim";
  trim.def.label = "Trim";
  trim.def.kind = Kind::Function;
  trim.def.inputs = {{.id = "text", .type = "text"}};
  trim.def.outputs = {{.id = "y", .type = "text"}};
  trim.def.params = {{.id = "mode", .type = "enum", .default_text = "both", .options = {"both", "start", "end"}}};
  trim.def.description = "Trims leading and/or trailing whitespace from text.";
  trim.def.view = {.compact = true, .glyph = ">|<", .value = "{mode}"};
  trim.make = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 0.0));
    return std::make_unique<Trim>(mode);
  };
  trim.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(trim));
}

}  // namespace paperboat::toolkit::string
