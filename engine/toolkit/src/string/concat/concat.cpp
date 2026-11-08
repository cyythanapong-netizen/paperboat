// string/concat.

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

class Concat final : public Block {
 public:
  Concat(std::string separator, int count) : separator_(std::move(separator)), count_(count) {}

  Values step(const Context&, const Values& in) override {
    std::string out;
    for (int i = 0; i < count_; ++i) {
      std::string id(1, static_cast<char>('a' + i));
      auto it = in.find(id);
      if (it != in.end()) {
        if (i > 0 && !separator_.empty()) out += separator_;
        out += value_to_text(it->second);
      }
    }
    return {{"y", out}};
  }

 private:
  std::string separator_;
  int count_;
};

}  // namespace

void add_concat(Registry& registry) {
  // string/concat
  auto concat = make_template(
      "string", "concat", "Concat", {"a", "b"}, {"y"},
      {{.id = "separator", .type = "text", .default_text = ""},
       {.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& p, const Config& c) {
        std::string sep = c.contains("separator") ? c.at("separator") : "";
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 1, 26);
        return std::make_unique<Concat>(std::move(sep), n);
      },
      "Concatenates its inputs formatted as text.", {.compact = true, .glyph = "++", .value = "{separator}"});
  for (auto& inp : concat.def.inputs) inp.type = "any";
  concat.def.outputs[0].type = "text";
  concat.def.ports_from = "inputs";
  registry.add(std::move(concat));
}

}  // namespace paperboat::toolkit::string
