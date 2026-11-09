// string/substring.

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

class Substring final : public Block {
 public:
  Substring(double start, double length) : default_start_(start), default_length_(length) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    double start_d = in.contains("start") ? get_or(in, "start") : default_start_;
    double len_d = in.contains("length") ? get_or(in, "length") : default_length_;

    long long n = static_cast<long long>(text.size());
    long long start = static_cast<long long>(start_d);
    if (start < 0) start += n;
    if (start < 0) start = 0;
    if (start > n) start = n;

    long long len;
    if (len_d < 0) {
      len = n - start;
    } else {
      len = std::min(static_cast<long long>(len_d), n - start);
    }
    return {{"y", text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(len))}};
  }

 private:
  double default_start_;
  double default_length_;
};

}  // namespace

void add_substring(Registry& registry) {
  // string/substring
  Template sub;
  sub.name = "string/substring";
  sub.def.type = "substring";
  sub.def.label = "Substring";
  sub.def.kind = Kind::Function;
  sub.def.inputs = {{.id = "text", .type = "text"}, {.id = "start", .type = "signal"}, {.id = "length", .type = "signal"}};
  sub.def.outputs = {{.id = "y", .type = "text"}};
  sub.def.params = {{.id = "start", .default_value = 0.0, .type = "number"},
                    {.id = "length", .default_value = -1.0, .type = "number"}};
  sub.def.description = "Extracts a substring from text.";
  sub.def.view = {.compact = true, .glyph = "[..]", .value = "{start}:{length}"};
  sub.make = [](const Values& p, const Config&) {
    double start = get_or(p, "start", 0.0);
    double len = get_or(p, "length", -1.0);
    return std::make_unique<Substring>(start, len);
  };
  sub.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(sub));
}

}  // namespace paperboat::toolkit::string
