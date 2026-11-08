// base/comment.

#include <stdexcept>
#include <string>

#include "common.hpp"

namespace paperboat::toolkit::base {

void add_comment(Registry& registry) {
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
