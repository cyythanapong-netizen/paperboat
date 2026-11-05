// Values of each datatype, set on the block: Number (a float), Text (a string) and Boolean
// (true or false, on the wire as 1 or 0). Every domain needs these.

#include <string>

#include "common.hpp"

namespace paperboat::toolkit::base {
namespace {

class TextValue final : public Block {
 public:
  explicit TextValue(std::string text) : text_(std::move(text)) {}
  Values step(const Context&, const Values&) override { return {{"y", text_}}; }

 private:
  std::string text_;
};

}  // namespace

void add_values(Registry& registry) {
  // "constant" for what it was called before it had siblings; shown as Number.
  auto number = make_template(
      "base", "constant", "Number", {}, {"y"}, {{"value", 1.0}}, [](const Values& p, const Config&) {
        double value = get_or(p, "value");
        return std::make_unique<Stateless>([=](const Context&, const Values&) { return Values{{"y", value}}; });
      }, "A number you set.", {.compact = true, .glyph = "const", .value = "{value}"});
  registry.add(std::move(number));

  Template text;
  text.name = "base/text";
  text.def = {.type = "text", .label = "Text", .outputs = {{"y"}},
              .params = {{.id = "value", .type = "text", .default_text = "hello"}},
              .description = "A piece of text you set."};
  text.make = [](const Values&, const Config& config) {
    auto it = config.find("value");
    return std::make_unique<TextValue>(it == config.end() ? "" : it->second);
  };
  registry.add(std::move(text));

  registry.add(make_template(
      "base", "boolean", "Boolean", {}, {"y"}, {{.id = "value", .default_value = 1, .type = "boolean"}},
      [](const Values& p, const Config&) {
        const double value = get_or(p, "value") != 0 ? 1.0 : 0.0;
        return std::make_unique<Stateless>([=](const Context&, const Values&) { return Values{{"y", value}}; });
      }, "On or off: 1 or 0."));
}

}  // namespace paperboat::toolkit::base
