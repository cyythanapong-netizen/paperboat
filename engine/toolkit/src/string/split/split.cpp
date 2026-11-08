// string/split.

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

class Split final : public Block {
 public:
  Split(std::string delim, double index) : default_delimiter_(std::move(delim)), default_index_(index) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string delim = in.contains("delimiter") ? value_to_text(in.at("delimiter")) : default_delimiter_;
    double index = in.contains("index") ? get_or(in, "index") : default_index_;

    if (text.empty()) {
      return {{"y", std::string("")}, {"vector", Vector{}}, {"count", 0.0}};
    }

    std::vector<std::string> parts;
    if (delim.empty()) {
      for (char c : text) parts.push_back(std::string(1, c));
    } else {
      std::size_t start = 0;
      while (start <= text.size()) {
        std::size_t pos = text.find(delim, start);
        if (pos == std::string::npos) {
          parts.push_back(text.substr(start));
          break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + delim.size();
      }
    }

    Vector vec;
    for (const auto& part : parts) {
      char* end = nullptr;
      double val = std::strtod(part.c_str(), &end);
      if (end != part.c_str() && *end == '\0') {
        vec.push_back(val);
      }
    }

    std::string out_y;
    if (index >= 0) {
      std::size_t idx = static_cast<std::size_t>(index);
      if (idx < parts.size()) out_y = parts[idx];
    } else {
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out_y += "\n";
        out_y += parts[i];
      }
    }

    return {{"y", out_y}, {"vector", vec}, {"count", static_cast<double>(parts.size())}};
  }

 private:
  std::string default_delimiter_;
  double default_index_;
};

}  // namespace

void add_split(Registry& registry) {
  // string/split
  Template split;
  split.name = "string/split";
  split.def.type = "split";
  split.def.label = "Split";
  split.def.kind = Kind::Function;
  split.def.inputs = {{.id = "text", .type = "text"}, {.id = "delimiter", .type = "text"}};
  split.def.outputs = {{.id = "y", .type = "text"}, {.id = "vector", .type = "vector"}, {.id = "count", .type = "signal"}};
  split.def.params = {{.id = "delimiter", .type = "text", .default_text = ","},
                      {.id = "index", .default_value = -1.0, .type = "number"}};
  split.def.description = "Splits text by delimiter into tokens, a numeric vector, and count.";
  split.def.view = {.compact = true, .glyph = "a|b", .value = "{delimiter}"};
  split.make = [](const Values& p, const Config& c) {
    std::string delim = c.contains("delimiter") ? c.at("delimiter") : ",";
    double index = get_or(p, "index", -1.0);
    return std::make_unique<Split>(std::move(delim), index);
  };
  registry.add(std::move(split));
}

}  // namespace paperboat::toolkit::string
