// Values of each datatype, set on the block: Number (a float), Text (a string) and Boolean
// (true or false, on the wire as 1 or 0). Every domain needs these.

#include <stdexcept>
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

// A variable's value on a wire (#75). It reads the run's variables each step, so a tunable one
// moves it during the run; any other is constant for the run.
class VariableValue final : public Block {
 public:
  explicit VariableValue(std::string name) : name_(std::move(name)) {}
  Values step(const Context& ctx, const Values&) override {
    const Value* v = ctx.variable(name_);
    if (!v) throw std::runtime_error(name_.empty() ? "pick a variable: its name is empty" : "no variable called \"" + name_ + "\"");
    return {{"y", *v}};
  }

 private:
  std::string name_;
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

  registry.add(make_template(
      "base", "variable", "Variable", {}, {"y"}, {{.id = "name", .type = "text"}},
      [](const Values&, const Config& text) {
        const auto it = text.find("name");
        return std::make_unique<VariableValue>(it == text.end() ? "" : it->second);
      },
      "A variable's value: constant for the run, or live if the variable is tunable.", {.compact = true, .glyph = "const", .value = "{name}"}));

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

  Template note;
  note.name = "base/comment";
  note.def = {.type = "comment", .label = "Note", .kind = Kind::Function, .inputs = {}, .outputs = {},
              .params = {{.id = "text", .type = "text", .default_text = "Note..."}},
              .description = "A canvas note or annotation card."};
  note.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values&) { return Values{}; });
  };
  registry.add(std::move(note));
}

}  // namespace paperboat::toolkit::base
