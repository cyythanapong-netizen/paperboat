// base/now.

#include <algorithm>
#include <deque>
#include <utility>

#include "common.hpp"

namespace paperboat::toolkit::base {
namespace {

class Now final : public Block {
 public:
  Values step(const Context& ctx, const Values&) override {
    return {{"y", ctx.t * 1000.0}};
  }
};

}  // namespace

void add_now(Registry& registry) {
  // base/now
  Template now_t;
  now_t.name = "base/now";
  now_t.def.type = "now";
  now_t.def.label = "Now";
  now_t.def.kind = Kind::Source;
  now_t.def.outputs = {{.id = "y", .type = "signal"}};
  now_t.def.description = "Current run clock in milliseconds.";
  now_t.def.view = {.compact = true, .glyph = "now"};
  now_t.make = [](const Values&, const Config&) { return std::make_unique<Now>(); };
  registry.add(std::move(now_t));
}

}  // namespace paperboat::toolkit::base
