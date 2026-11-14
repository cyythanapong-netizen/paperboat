// base/if: If: chooses a value (signal), or a way (step)

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "base/logic_shared.hpp"

namespace paperboat::toolkit::base {
namespace {

// If, signal form: chooses a or b, passing the value through as it is.
class Choose final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    const auto pick = in.find(holds(in, "cond") ? "a" : "b");
    if (pick == in.end()) return {{"y", 0.0}};  // the chosen input isn't wired: reads as 0
    return {{"y", pick->second}};
  }
};

}  // namespace

void add_if(Registry& registry) {
  // If: chooses a value (signal), or a way (step).
  Template t;
  t.name = "base/if";
  t.def = magic_def("if", "If",
                    {{.name = "signal", .inputs = {{"cond"}, {"a", "any"}, {"b", "any"}}, .outputs = {{"y", "any"}}},
                     {.name = "step", .inputs = {{"in", "step"}, {"cond"}}, .outputs = {{"yes", "step"}, {"no", "step"}}}});
  t.def.description = "Picks a or b by a condition; on a Step flow, branches the path.";
  t.make = [](const Values&, const Config&) { return std::make_unique<Choose>(); };
  t.make_step = [](const Values& p, const Config&) {
    return std::make_unique<Branch>([](const Values& in, const Values&) { return holds(in, "cond"); }, p);
  };
  t.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  t.def.view = {.compact = true, .icon = nlohmann::json::parse(R"({"w":1,"h":1,"g":[{"t":"line","p":[[10,30],[35,30]]},{"t":"line","p":[[10,70],[35,70]]},{"t":"ellipse","x":30,"y":25,"w":10,"h":10,"fill":"ink"},{"t":"ellipse","x":30,"y":65,"w":10,"h":10,"fill":"ink"},{"t":"line","p":[[35,30],[70,50]]},{"t":"line","p":[[70,50],[90,50]]},{"t":"line","p":[[50,10],[50,35]],"stroke":"muted","dash":"dash"}]})")};  // a selector switch; a Step-flow If keeps its card
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::base
