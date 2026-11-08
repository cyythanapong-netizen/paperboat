// signal/generator: The parent: f(t) as an expression `f` (config, or a text param a user edits) over t and

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "paperboat/toolkit/expression.hpp"

namespace paperboat::toolkit::signal {
namespace {

// f(t) from an expression over t, the block's number params (Sine, Step…), and its
// wired inputs (f(t)).
class Expression final : public Block {
 public:
  Expression(const BlockDef& def, const Values& params, const Config& config)
      : f_(text_of(config), names(def, text_of(config))),
        params_(params),
        values_(f_.variables().size(), 0.0) {}

  Values step(const Context& ctx, const Values& in) override {
    const auto& vars = f_.variables();
    for (std::size_t i = 0; i < vars.size(); ++i) {
      if (vars[i] == "t") values_[i] = in.contains("t") ? get_or(in, "t") : ctx.t;
      else if (params_.contains(vars[i])) values_[i] = get_or(params_, vars[i]);
      else values_[i] = get_or(in, vars[i]);
    }
    return {{"y", f_(values_)}};
  }

  // For a block with `inputs_from` (f(t)), any free name in `text` is an input; for a
  // fixed-formula generator (Sine, Step…), only `t` and the block's number/enum params.
  static std::vector<std::string> names(const BlockDef& def, std::string_view text) {
    if (!def.inputs_from.empty()) return toolkit::Expression::free_names(text);
    std::vector<std::string> out{"t"};
    for (const auto& p : def.params)
      if (p.is_number() || p.type == "enum") out.push_back(p.id);  // an enum by its index
    return out;
  }
  static std::string text_of(const Config& config) {
    auto it = config.find("f");
    return it == config.end() ? "" : it->second;
  }

 private:
  toolkit::Expression f_;
  Values params_;
  std::vector<double> values_;
};

}  // namespace

void add_generator(Registry& registry) {
  // The parent: f(t) as an expression `f` (config, or a text param a user edits) over t and
  // the params a definition adds. Not in a palette by itself.
  Template generator;
  generator.name = "signal/generator";
  generator.def = generator_def("generator", "Generator");
  generator.def.elementwise = true;
  // each variable a vector or a number, element by element
  generator.config = {{"f", "0"}};
  generator.extendable = true;
  generator.abstract = true;
  generator.check = [](const BlockDef& def, const Config& config) {
    try {
      const auto text = Expression::text_of(config);
      const toolkit::Expression f(text, Expression::names(def, text));
    } catch (const ExpressionError& err) {
      throw ExpressionError("f: " + std::string(err.what()));
    }
  };
  // Each definition on it makes its own Template (a copy); fixed-formula generators check
  // their names at load time, while `inputs_from` blocks take any free names in `f`.
  generator.make = [](const Values& params, const Config& config) -> std::unique_ptr<Block> {
    BlockDef def;
    def.inputs_from = "f";
    try {
      return std::make_unique<Expression>(def, params, config);
    } catch (const ExpressionError& err) {
      throw ExpressionError("f: " + std::string(err.what()));
    }
  };
  registry.add(std::move(generator));
}

}  // namespace paperboat::toolkit::signal
