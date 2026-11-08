// base/text.

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

}  // namespace

void add_text(Registry& registry) {
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
}

}  // namespace paperboat::toolkit::base
