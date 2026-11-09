// string/change_case.

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

class ChangeCase final : public Block {
 public:
  explicit ChangeCase(int mode) : mode_(mode) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    if (mode_ == 0) {
      for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (mode_ == 1) {
      for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (mode_ == 2) {
      bool new_word = true;
      for (char& c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || std::ispunct(static_cast<unsigned char>(c))) {
          new_word = true;
        } else if (new_word) {
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          new_word = false;
        } else {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
    }
    return {{"y", text}};
  }

 private:
  int mode_;
};

}  // namespace

void add_change_case(Registry& registry) {
  // string/change_case
  Template cc;
  cc.name = "string/change_case";
  cc.def.type = "change_case";
  cc.def.label = "Change Case";
  cc.def.kind = Kind::Function;
  cc.def.inputs = {{.id = "text", .type = "text"}};
  cc.def.outputs = {{.id = "y", .type = "text"}};
  cc.def.params = {{.id = "mode", .type = "enum", .default_text = "lower", .options = {"upper", "lower", "title"}}};
  cc.def.description = "Converts text to upper, lower, or title case.";
  cc.def.view = {.compact = true, .glyph = "aA", .value = "{mode}"};
  cc.make = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 1.0));
    return std::make_unique<ChangeCase>(mode);
  };
  cc.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(cc));
}

}  // namespace paperboat::toolkit::string
