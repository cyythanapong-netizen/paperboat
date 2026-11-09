// base/type_of.

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

class TypeOf final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    auto it = in.find("in");
    if (it == in.end()) return {{"y", std::string("empty")}};

    const Value& val = it->second;
    if (std::holds_alternative<double>(val)) return {{"y", std::string("number")}};
    if (std::holds_alternative<std::string>(val)) return {{"y", std::string("text")}};
    if (std::holds_alternative<Vector>(val)) return {{"y", std::string("vector")}};
    if (std::holds_alternative<Table>(val)) return {{"y", std::string("table")}};
    if (const Handle* h = std::get_if<Handle>(&val)) {
      return {{"y", h->kind.empty() ? std::string("handle") : h->kind}};
    }
    return {{"y", std::string("empty")}};
  }
};

}  // namespace

void add_type_of(Registry& registry) {
  // base/type_of
  Template type_of;
  type_of.name = "base/type_of";
  type_of.def.type = "type_of";
  type_of.def.label = "Type Of";
  type_of.def.kind = Kind::Function;
  type_of.def.inputs = {{.id = "in", .type = "any"}};
  type_of.def.outputs = {{.id = "y", .type = "text"}};
  type_of.def.description = "Returns the type name: number, text, vector, table, handle, or empty.";
  type_of.def.view = {.compact = true, .glyph = "type"};
  type_of.make = [](const Values&, const Config&) { return std::make_unique<TypeOf>(); };
  type_of.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(type_of));
}

}  // namespace paperboat::toolkit::base
