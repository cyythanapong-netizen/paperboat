#pragma once

// Shared by several base blocks.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::base {

using Test = std::function<bool(const Values& in, const Values& params)>;

inline bool holds(const Values& in, std::string_view id) { return get_or(in, id) > 0; }

class LogicValue final : public Block {
 public:
  explicit LogicValue(Test test, Values params) : test_(std::move(test)), params_(std::move(params)) {}
  Values step(const Context&, const Values& in) override { return {{"y", test_(in, params_) ? 1.0 : 0.0}}; }

 private:
  Test test_;
  Values params_;
};

class Branch final : public Step {
 public:
  explicit Branch(Test test, Values params) : test_(std::move(test)), params_(std::move(params)) {}
  std::string tick(const Context&, const Values& in) override { return test_(in, params_) ? "yes" : "no"; }

 private:
  Test test_;
  Values params_;
};

inline std::vector<PortDef> signals(std::initializer_list<const char*> ids) {
  std::vector<PortDef> out;
  for (const char* id : ids) out.push_back({id});
  return out;
}

// The two forms of a test on `inputs`: signal → y, step: in, inputs → yes, no.
inline std::vector<Form> forms(std::initializer_list<const char*> inputs) {
  auto step_in = signals(inputs);
  step_in.insert(step_in.begin(), {"in", "step"});
  return {{.name = "signal", .inputs = signals(inputs), .outputs = {{"y"}}},
          {.name = "step", .inputs = step_in, .outputs = {{"yes", "step"}, {"no", "step"}}}};
}

inline void add_test(Registry& registry, const std::string& type, const std::string& label,
              std::initializer_list<const char*> inputs, std::vector<ParamDef> params, Test test,
              std::string description, View view = {}, std::string ports_from = {}) {
  Template t;
  t.name = "base/" + type;
  t.def = magic_def(type, label, forms(inputs), std::move(params));
  t.def.description = std::move(description);
  t.def.view = std::move(view);
  t.def.ports_from = std::move(ports_from);
  t.make = [test](const Values& p, const Config&) { return std::make_unique<LogicValue>(test, p); };
  t.make_step = [test](const Values& p, const Config&) { return std::make_unique<Branch>(test, p); };
  t.def.cacheable = true;  // a pure function of its params and inputs (a run-once reuses it)
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::base
