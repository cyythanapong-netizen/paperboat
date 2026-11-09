// base/variable.

#include <stdexcept>
#include <string>

#include "common.hpp"
#include "base/constant/icon.hpp"

namespace paperboat::toolkit::base {
namespace {

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

void add_variable(Registry& registry) {
  auto t = make_template(
      "base", "variable", "Variable", {}, {"y"}, {{.id = "name", .type = "variable"}},
      [](const Values&, const Config& text) {
        const auto it = text.find("name");
        return std::make_unique<VariableValue>(it == text.end() ? "" : it->second);
      },
      "A variable's value: constant for the run, or live if the variable is tunable.", {.compact = true, .glyph = "const", .value = "{name}", .icon = constant_icon()});
  // Reads a variable through Context::variable, which the run records: a run-once reuses it
  // while that variable holds the same value.
  t.def.cacheable = true;
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::base
