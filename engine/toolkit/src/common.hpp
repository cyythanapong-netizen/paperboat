#pragma once

// Private to the toolkit: helpers each package uses to define and register its templates.

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit {

// A stateless block: outputs depend only on the clock and the inputs.
class Stateless : public Block {
 public:
  using Fn = std::function<Values(const Context&, const Values&)>;
  explicit Stateless(Fn fn) : fn_(std::move(fn)) {}
  Values step(const Context& ctx, const Values& in) override { return fn_(ctx, in); }
  bool stateless() const override { return true; }

 private:
  Fn fn_;
};

// A <package>/<type> function template with signal ports.
inline Template make_template(const std::string& package, std::string type, std::string label,
                              std::vector<std::string> inputs, std::vector<std::string> outputs,
                              std::vector<ParamDef> params, decltype(Template::make) make,
                              std::string description = {}, View view = {}) {
  Template t;
  t.name = package + "/" + type;
  t.def.type = std::move(type);
  t.def.label = std::move(label);
  t.def.kind = Kind::Function;
  for (auto& id : inputs) t.def.inputs.push_back({std::move(id)});
  for (auto& id : outputs) t.def.outputs.push_back({std::move(id)});
  t.def.params = std::move(params);
  t.def.description = std::move(description);
  t.def.view = std::move(view);
  t.make = std::move(make);
  return t;
}

}  // namespace paperboat::toolkit
