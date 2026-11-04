#pragma once

// Private to the toolkit: helpers every package uses to define its templates, and the
// parts each package's register_<package>() calls.

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit {

// The built-in templates' one-line descriptions, by name ("control/pid"); "" if none.
std::string builtin_description(std::string_view name);

// A stateless block: outputs depend only on the clock and the inputs.
class Stateless : public Block {
 public:
  using Fn = std::function<Values(const Context&, const Values&)>;
  explicit Stateless(Fn fn) : fn_(std::move(fn)) {}
  Values step(const Context& ctx, const Values& in) override { return fn_(ctx, in); }

 private:
  Fn fn_;
};

// A <package>/<type> function template with signal ports.
inline Template make_template(const std::string& package, std::string type, std::string label,
                              std::vector<std::string> inputs, std::vector<std::string> outputs,
                              std::vector<ParamDef> params, decltype(Template::make) make) {
  Template t;
  t.name = package + "/" + type;
  t.def.type = std::move(type);
  t.def.label = std::move(label);
  t.def.kind = Kind::Function;
  for (auto& id : inputs) t.def.inputs.push_back({std::move(id)});
  for (auto& id : outputs) t.def.outputs.push_back({std::move(id)});
  t.def.params = std::move(params);
  t.make = std::move(make);
  return t;
}

// base: what every domain needs, in every flow type.
namespace base {
void add_values(Registry& registry);    // constant (Number), text, boolean
void add_key(Registry& registry);       // key
void add_logic(Registry& registry);     // if, and, or, not, compare: magic (signal | step)
}  // namespace base

// signal: generators driven by the run clock.
namespace signal {
void add_waves(Registry& registry);  // clock, generator (the parent of blocks.toml's)
}  // namespace signal

// math: pointwise arithmetic on signals.
namespace math {
void add_arithmetic(Registry& registry);  // gain, sum, threshold, saturation
}  // namespace math

// dashboard: monitors, shown on the Dashboard as instruments.
namespace dashboard {
void add_instruments(Registry& registry);  // graph, display
}  // namespace dashboard

// step: the blocks of a Step flow's path.
namespace step {
void add_flow(Registry& registry);  // start, when, wait, if, repeat, stop
}  // namespace step

// control: dynamics that run on a clock in a signal flow.
namespace control {
void add_pid(Registry& registry);        // pid
void add_simulator(Registry& registry);  // integrator, lag
}  // namespace control

}  // namespace paperboat::toolkit
