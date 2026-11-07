// Type and conversion blocks: to_number, to_text, to_boolean, type_of.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common.hpp"

namespace paperboat::toolkit::base {
namespace {

std::string trim_spaces(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return std::string(s);
}

std::string to_lower_trimmed(std::string_view s) {
  std::string out = trim_spaces(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

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

class ToText final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    auto it = in.find("in");
    if (it == in.end()) return {{"y", std::string("")}};
    return {{"y", value_to_text(it->second)}};
  }
};

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

void add_conversion(Registry& registry) {
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
  registry.add(std::move(to_num));

  // base/to_text
  Template to_txt;
  to_txt.name = "base/to_text";
  to_txt.def.type = "to_text";
  to_txt.def.label = "To Text";
  to_txt.def.kind = Kind::Function;
  to_txt.def.inputs = {{.id = "in", .type = "any"}};
  to_txt.def.outputs = {{.id = "y", .type = "text"}};
  to_txt.def.description = "Converts numbers, booleans, or lists to formatted text.";
  to_txt.def.view = {.compact = true, .glyph = "→T"};
  to_txt.make = [](const Values&, const Config&) { return std::make_unique<ToText>(); };
  registry.add(std::move(to_txt));

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
  registry.add(std::move(to_bool));

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
  registry.add(std::move(type_of));

  // base/mux and base/demux (#164): numbers into one vector and back. Their ports are the
  // `names` ("x v theta"); `units` ("m m/s rad") gives each a unit, by position.
  const std::vector<ParamDef> named = {{.id = "names", .type = "text", .default_text = "a b"},
                                       {.id = "units", .type = "text", .default_text = ""}};
  Template mux;
  mux.name = "base/mux";
  mux.def.type = "mux";
  mux.def.label = "Mux";
  mux.def.inputs = {{.id = "a"}, {.id = "b"}};
  mux.def.outputs = {vector_port("y", {{.id = "a"}, {.id = "b"}})};
  mux.def.params = named;
  mux.def.ports_from = "names";
  mux.def.description = "Joins numbers into one vector, one input per name.";
  mux.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "a b", {"y"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      Vector y;
      y.reserve(names.size());
      for (const auto& n : names) y.push_back(get_or(in, n));
      return Values{{"y", std::move(y)}};
    });
  };
  registry.add(std::move(mux));

  Template demux;
  demux.name = "base/demux";
  demux.def.type = "demux";
  demux.def.label = "Demux";
  demux.def.inputs = {vector_port("u", {{.id = "a"}, {.id = "b"}})};
  demux.def.outputs = {{.id = "a"}, {.id = "b"}};
  demux.def.params = named;
  demux.def.outputs_from = "names";
  demux.def.description = "Splits a vector into numbers, one output per name.";
  demux.make = [](const Values&, const Config& config) {
    const auto names = parse_names(config.contains("names") ? config.at("names") : "a b", {"u"});
    return std::make_unique<Stateless>([names](const Context&, const Values& in) {
      const Vector u = as_vector(in, "u");  // unwired: zeros
      Values out;
      for (std::size_t i = 0; i < names.size(); ++i) out[names[i]] = i < u.size() ? u[i] : 0.0;
      return out;
    });
  };
  registry.add(std::move(demux));
}

}  // namespace paperboat::toolkit::base
