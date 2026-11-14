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
  text.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  text.def.view = {.compact = true, .value = "{value}", .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"text","x":50,"y":50,"s":"“T”","size":15,"font":"mono"}]})")};  // literal text, in quotes
  registry.add(std::move(text));
}

}  // namespace paperboat::toolkit::base
