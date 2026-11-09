// base/to_number.

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

class ToNumber final : public Block {
 public:
  explicit ToNumber(double fallback_param) : fallback_param_(fallback_param) {}

  Values step(const Context&, const Values& in) override {
    double fb = in.contains("fallback") ? get_or(in, "fallback") : fallback_param_;
    auto it = in.find("in");
    if (it == in.end()) return {{"y", fb}};

    const Value& val = it->second;
    if (const double* d = std::get_if<double>(&val)) {
      return {{"y", std::isfinite(*d) ? *d : fb}};
    }
    if (const std::string* s = std::get_if<std::string>(&val)) {
      std::string lower = to_lower_trimmed(*s);
      if (lower == "true") return {{"y", 1.0}};
      if (lower == "false") return {{"y", 0.0}};
      if (lower.empty()) return {{"y", fb}};
      char* end = nullptr;
      double parsed = std::strtod(lower.c_str(), &end);
      if (end != lower.c_str() && *end == '\0' && std::isfinite(parsed)) {
        return {{"y", parsed}};
      }
      return {{"y", fb}};
    }
    return {{"y", fb}};
  }

 private:
  double fallback_param_;
};

}  // namespace

void add_to_number(Registry& registry) {
  // base/to_number
  Template to_num;
  to_num.name = "base/to_number";
  to_num.def.type = "to_number";
  to_num.def.label = "To Number";
  to_num.def.kind = Kind::Function;
  to_num.def.inputs = {{.id = "in", .type = "any"}, {.id = "fallback", .type = "signal"}};
  to_num.def.outputs = {{.id = "y", .type = "signal"}};
  to_num.def.params = {{.id = "fallback", .default_value = 0.0, .type = "number"}};
  to_num.def.description = "Converts any value or text to a number, or uses a fallback.";
  to_num.def.view = {.compact = true, .glyph = "→#", .value = "{fallback}"};
  to_num.make = [](const Values& params, const Config&) {
    return std::make_unique<ToNumber>(get_or(params, "fallback", 0.0));
  };
  to_num.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(to_num));
}

}  // namespace paperboat::toolkit::base
