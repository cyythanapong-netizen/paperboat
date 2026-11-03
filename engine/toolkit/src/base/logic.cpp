// Logic: magic blocks, each in two forms, picked by its `form` param.
//   signal  computes, in a signal flow: And, Or, Not, Compare give 1 or 0; If gives a when
//           cond holds, else b (any value: a number, a list, text…).
//   step    branches, on a Step flow's path: in → yes when the same test holds, else no.
// A value holds when it is positive (a Key held, a Threshold over its level).

#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::base {
namespace {

using Test = std::function<bool(const Values& in, const Values& params)>;

bool holds(const Values& in, std::string_view id) { return get_or(in, id) > 0; }

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

// If, signal form: chooses a or b, passing the value through as it is.
class Choose final : public Block {
 public:
  Values step(const Context&, const Values& in) override {
    const auto pick = in.find(holds(in, "cond") ? "a" : "b");
    if (pick == in.end()) return {{"y", 0.0}};  // the chosen input isn't wired: reads as 0
    return {{"y", pick->second}};
  }
};

std::vector<PortDef> signals(std::initializer_list<const char*> ids) {
  std::vector<PortDef> out;
  for (const char* id : ids) out.push_back({id});
  return out;
}

// The two forms of a test on `inputs`: signal → y, step: in, inputs → yes, no.
std::vector<Form> forms(std::initializer_list<const char*> inputs) {
  auto step_in = signals(inputs);
  step_in.insert(step_in.begin(), {"in", "step"});
  return {{.name = "signal", .inputs = signals(inputs), .outputs = {{"y"}}},
          {.name = "step", .inputs = step_in, .outputs = {{"yes", "step"}, {"no", "step"}}}};
}

void add_test(Registry& registry, const std::string& type, const std::string& label,
              std::initializer_list<const char*> inputs, std::vector<ParamDef> params, Test test) {
  Template t;
  t.name = "base/" + type;
  t.def = magic_def(type, label, forms(inputs), std::move(params));
  t.make = [test](const Values& p, const Config&) { return std::make_unique<LogicValue>(test, p); };
  t.make_step = [test](const Values& p, const Config&) { return std::make_unique<Branch>(test, p); };
  registry.add(std::move(t));
}

}  // namespace

void add_logic(Registry& registry) {
  // If: chooses a value (signal), or a way (step).
  Template t;
  t.name = "base/if";
  t.def = magic_def("if", "If",
                    {{.name = "signal", .inputs = signals({"cond", "a", "b"}), .outputs = {{"y"}}},
                     {.name = "step", .inputs = {{"in", "step"}, {"cond"}}, .outputs = {{"yes", "step"}, {"no", "step"}}}});
  t.make = [](const Values&, const Config&) { return std::make_unique<Choose>(); };
  t.make_step = [](const Values& p, const Config&) {
    return std::make_unique<Branch>([](const Values& in, const Values&) { return holds(in, "cond"); }, p);
  };
  registry.add(std::move(t));

  add_test(registry, "and", "And", {"a", "b"}, {},
           [](const Values& in, const Values&) { return holds(in, "a") && holds(in, "b"); });
  add_test(registry, "or", "Or", {"a", "b"}, {},
           [](const Values& in, const Values&) { return holds(in, "a") || holds(in, "b"); });
  add_test(registry, "not", "Not", {"u"}, {}, [](const Values& in, const Values&) { return !holds(in, "u"); });
  add_test(registry, "compare", "Compare", {"a", "b"},
           {{.id = "op", .type = "enum", .default_text = ">", .options = {">", ">=", "<", "<=", "==", "!="}}},
           [](const Values& in, const Values& p) {
             const double a = get_or(in, "a"), b = get_or(in, "b");
             switch (static_cast<int>(get_or(p, "op"))) {  // the op's index in its options
               case 0: return a > b;
               case 1: return a >= b;
               case 2: return a < b;
               case 3: return a <= b;
               case 4: return a == b;
               default: return a != b;
             }
           });
}

}  // namespace paperboat::toolkit::base
