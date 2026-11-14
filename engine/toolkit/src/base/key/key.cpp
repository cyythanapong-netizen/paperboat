// base/key.

#include <string>
#include <utility>

#include "common.hpp"
#include "base/key_shared.hpp"

namespace paperboat::toolkit::base {

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
  t.def.cacheable = false;  // reads the keys held in the UI: never reused from an earlier run
  t.def.view = {.compact = true, .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"rect","x":5,"y":25,"w":90,"h":50,"corner":"sm"},{"t":"line","p":[[10,65],[90,65]],"weight":"thin"},{"t":"text","x":50,"y":45,"s":"{key}","size":12,"font":"mono"}]})")};  // a keycap with the key's name
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::base
