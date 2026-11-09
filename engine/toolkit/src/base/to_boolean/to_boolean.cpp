// base/to_boolean.

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

class ToBoolean final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    auto it = in.find("in");
    if (it == in.end()) return {{"y", 0.0}};

    const Value& val = it->second;
    if (const double* d = std::get_if<double>(&val)) {
      return {{"y", (*d != 0.0 && !std::isnan(*d)) ? 1.0 : 0.0}};
    }
    if (const std::string* s = std::get_if<std::string>(&val)) {
      std::string lower = to_lower_trimmed(*s);
      if (lower.empty() || lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return {{"y", 0.0}};
      }
      return {{"y", 1.0}};
    }
    if (const Vector* vec = std::get_if<Vector>(&val)) {
      return {{"y", vec->empty() ? 0.0 : 1.0}};
    }
    if (const Table* tbl = std::get_if<Table>(&val)) {
      return {{"y", tbl->empty() ? 0.0 : 1.0}};
    }
    if (const Handle* h = std::get_if<Handle>(&val)) {
      return {{"y", h->data != nullptr ? 1.0 : 0.0}};
    }
    return {{"y", 0.0}};
  }
};

}  // namespace

void add_to_boolean(Registry& registry) {
  // base/to_boolean
  Template to_bool;
  to_bool.name = "base/to_boolean";
  to_bool.def.type = "to_boolean";
  to_bool.def.label = "To Boolean";
  to_bool.def.kind = Kind::Function;
  to_bool.def.inputs = {{.id = "in", .type = "any"}};
  to_bool.def.outputs = {{.id = "y", .type = "signal"}};
  to_bool.def.description = "Truthiness coercion: non-zero numbers and non-empty values become 1, else 0.";
  to_bool.def.view = {.compact = true, .glyph = "?0/1"};
  to_bool.make = [](const Values&, const Config&) { return std::make_unique<ToBoolean>(); };
  to_bool.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(to_bool));
}

}  // namespace paperboat::toolkit::base
