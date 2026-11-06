// Blocks driven by the UI's `input` messages.

#include <string>
#include <utility>

#include "common.hpp"

namespace paperboat::toolkit::base {
namespace {

class Key : public Block {
 public:
  explicit Key(std::string key) : key_(std::move(key)) {}
  Values step(const Context& ctx, const Values&) override {
    bool down = ctx.keys_down != nullptr && ctx.keys_down->contains(key_);
    return {{"pressed", down ? 1.0 : 0.0}};
  }

 private:
  std::string key_;
};

class KeyHold : public Block {
 public:
  explicit KeyHold(std::string key) : key_(std::move(key)) {}
  Values step(const Context& ctx, const Values&) override {
    bool down = ctx.keys_down != nullptr && ctx.keys_down->contains(key_);
    if (down) {
      duration_ += ctx.dt;
    } else {
      duration_ = 0.0;
    }
    return {{"held", down ? 1.0 : 0.0}, {"duration", duration_}};
  }

 private:
  std::string key_;
  double duration_ = 0.0;
};

}  // namespace

void add_key(Registry& registry) {
  Template t;
  t.name = "base/key";
  t.def.type = "key";
  t.def.label = "Key";
  t.def.kind = Kind::Function;
  t.def.outputs = {{"pressed"}};
  t.def.description = "1 while a key is held down in the app.";
  // Which key: a KeyboardEvent.code, set on the block in the UI (or its default in a
  // descriptor's `defaults`). It reaches make() through `config`, like every text param.
  t.def.params = {{.id = "key", .default_value = 0.0, .type = "key", .default_text = "Space"}};
  t.make = [](const Values&, const Config& config) {
    auto it = config.find("key");
    return std::make_unique<Key>(it == config.end() ? "Space" : it->second);
  };
  registry.add(std::move(t));

  Template hold;
  hold.name = "base/key_hold";
  hold.def.type = "key_hold";
  hold.def.label = "Key Hold";
  hold.def.kind = Kind::Function;
  hold.def.outputs = {{.id = "held", .type = "signal"}, {.id = "duration", .type = "signal"}};
  hold.def.description = "Outputs 1 while a key is held, plus the duration in seconds.";
  hold.def.params = {{.id = "key", .default_value = 0.0, .type = "key", .default_text = "Space"}};
  hold.def.view = {.compact = true, .glyph = "⌨", .value = "{key}"};
  hold.make = [](const Values&, const Config& config) {
    auto it = config.find("key");
    return std::make_unique<KeyHold>(it == config.end() ? "Space" : it->second);
  };
  registry.add(std::move(hold));
}

}  // namespace paperboat::toolkit::base
