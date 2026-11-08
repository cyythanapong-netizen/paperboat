// step/say.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"
#include "step/flow_shared.hpp"

namespace paperboat::toolkit::step {
namespace {

// A value as a line of text: a number, text, a short list, a table's size, a handle's kind.
std::string say(const Value& v) {
  char number[32];
  auto num = [&](double x) {
    std::snprintf(number, sizeof number, "%g", x);
    return std::string(number);
  };
  if (const double* x = std::get_if<double>(&v)) return num(*x);
  if (const std::string* text = std::get_if<std::string>(&v)) return *text;
  if (const List* list = std::get_if<List>(&v)) {
    constexpr std::size_t kShown = 8;
    std::string out = "[";
    for (std::size_t i = 0; i < std::min(list->size(), kShown); ++i) out += (i ? ", " : "") + num((*list)[i]);
    if (list->size() > kShown) out += ", … " + std::to_string(list->size()) + " in all";
    return out + "]";
  }
  if (const Table* table = std::get_if<Table>(&v))
    return "table " + std::to_string(table->size()) + " × " + std::to_string(table->empty() ? 0 : table->front().size());
  return "(" + std::get<Handle>(v).kind + ")";
}

// Writes a line to the Console as the path passes: its text, then `value` if wired.
class Say final : public Step {
 public:
  explicit Say(std::string text) : text_(std::move(text)) {}
  void enter(const Context& ctx, const Values& in) override {
    auto it = in.find("value");
    if (it == in.end()) ctx.log(text_);
    else ctx.log(text_.empty() ? say(it->second) : text_ + " " + say(it->second));
  }
  std::string tick(const Context&, const Values&) override { return "next"; }

 private:
  std::string text_;
};

}  // namespace

void add_say(Registry& registry) {
  registry.add(make_step_template("say", "Say", {step_port("in"), {"value", "any"}}, {step_port("next")},
                                  {{.id = "text", .type = "text", .default_text = "hello"}},
                                  [](const Values&, const Config& c) {
                                     auto it = c.find("text");
                                     return std::make_unique<Say>(it == c.end() ? "hello" : it->second);
                                   }, "Writes a line to the Console as the path passes."));
}

}  // namespace paperboat::toolkit::step
