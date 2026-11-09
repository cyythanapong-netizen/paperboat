// string/replace.

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

class Replace final : public Block {
 public:
  Replace(std::string find, std::string rep, bool all)
      : default_find_(std::move(find)), default_rep_(std::move(rep)), all_(all) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string find = in.contains("find") ? value_to_text(in.at("find")) : default_find_;
    std::string rep = in.contains("replace") ? value_to_text(in.at("replace")) : default_rep_;

    if (find.empty()) return {{"y", text}};

    std::string out;
    if (all_) {
      std::size_t start = 0;
      while (start < text.size()) {
        std::size_t pos = text.find(find, start);
        if (pos == std::string::npos) {
          out.append(text, start, text.size() - start);
          break;
        }
        out.append(text, start, pos - start);
        out.append(rep);
        start = pos + find.size();
      }
    } else {
      std::size_t pos = text.find(find);
      if (pos != std::string::npos) {
        out = text.substr(0, pos) + rep + text.substr(pos + find.size());
      } else {
        out = text;
      }
    }
    return {{"y", out}};
  }

 private:
  std::string default_find_;
  std::string default_rep_;
  bool all_;
};

}  // namespace

void add_replace(Registry& registry) {
  // string/replace
  Template rep;
  rep.name = "string/replace";
  rep.def.type = "replace";
  rep.def.label = "Replace";
  rep.def.kind = Kind::Function;
  rep.def.inputs = {{.id = "text", .type = "text"}, {.id = "find", .type = "text"}, {.id = "replace", .type = "text"}};
  rep.def.outputs = {{.id = "y", .type = "text"}};
  rep.def.params = {{.id = "find", .type = "text", .default_text = ""},
                    {.id = "replace", .type = "text", .default_text = ""},
                    {.id = "all", .default_value = 1.0, .type = "boolean"}};
  rep.def.description = "Replaces occurrences of find with replace in text.";
  rep.def.view = {.compact = true, .glyph = "a→b", .value = "{find}→{replace}"};
  rep.make = [](const Values& p, const Config& c) {
    std::string find = c.contains("find") ? c.at("find") : "";
    std::string replace = c.contains("replace") ? c.at("replace") : "";
    bool all = get_or(p, "all", 1.0) != 0.0;
    return std::make_unique<Replace>(std::move(find), std::move(replace), all);
  };
  rep.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(rep));
}

}  // namespace paperboat::toolkit::string
