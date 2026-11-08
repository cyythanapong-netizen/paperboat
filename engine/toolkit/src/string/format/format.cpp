// string/format.

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

class Format final : public Block {
 public:
  explicit Format(std::string tmpl) : template_(std::move(tmpl)) {}

  Values step(const Context&, const Values& in) override {
    std::string out;
    out.reserve(template_.size() + 16);
    for (std::size_t i = 0; i < template_.size(); ++i) {
      if (template_[i] == '{') {
        if (i + 1 < template_.size() && template_[i + 1] == '{') {
          out.push_back('{');
          ++i;
          continue;
        }
        auto end = template_.find('}', i + 1);
        if (end == std::string::npos) {
          out.push_back('{');
          continue;
        }
        auto inner = template_.find('{', i + 1);
        if (inner != std::string::npos && inner < end) {
          out.push_back('{');
          continue;
        }
        std::string_view token = std::string_view(template_).substr(i + 1, end - (i + 1));
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
        std::string key(token);
        auto it = in.find(key);
        if (it != in.end()) {
          out += value_to_text(it->second);
        }
        i = end;
      } else if (template_[i] == '}') {
        if (i + 1 < template_.size() && template_[i + 1] == '}') {
          out.push_back('}');
          ++i;
          continue;
        }
        out.push_back('}');
      } else {
        out.push_back(template_[i]);
      }
    }
    return {{"y", out}};
  }

 private:
  std::string template_;
};

}  // namespace

void add_format(Registry& registry) {
  // string/format
  Template fmt;
  fmt.name = "string/format";
  fmt.def.type = "format";
  fmt.def.label = "Format";
  fmt.def.kind = Kind::Function;
  fmt.def.outputs = {{.id = "y", .type = "text"}};
  fmt.def.params = {{.id = "template", .type = "text", .default_text = "Hello {name}"}};
  fmt.def.description = "Fills template string with values from dynamic input sockets.";
  fmt.def.ports_from = "template";
  fmt.def.view = {.compact = true, .glyph = "{…}", .value = "{template}"};
  fmt.make = [](const Values&, const Config& c) {
    std::string tmpl = c.contains("template") ? c.at("template") : "Hello {name}";
    return std::make_unique<Format>(std::move(tmpl));
  };
  registry.add(std::move(fmt));
}

}  // namespace paperboat::toolkit::string
