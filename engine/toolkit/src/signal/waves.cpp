// The signal package's C++: Clock, and the parent of every other signal block, Generator,
// whose f(t) is an expression. Sine, Step, Ramp, Square, Ease and f(t) are only
// definitions on it (blocks.toml, next to this file): params and a fixed `f`.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "paperboat/toolkit/expression.hpp"

namespace paperboat::toolkit::signal {
namespace {

// The run time itself: f(t) = t (end a run after 10 s: Clock → Threshold → When → Stop).
class Clock final : public Generator {
 public:
  Clock() : Generator("t") {}
  double value(double t) const override { return t; }
};

// f(t) from an expression over t and the block's number params.
class Expression final : public Generator {
 public:
  Expression(const BlockDef& def, const Values& params, const Config& config)
      : f_(text_of(config), names(def)) {
    values_.assign(f_.variables().size(), 0.0);
    for (std::size_t i = 1; i < values_.size(); ++i) values_[i] = get_or(params, f_.variables()[i]);
  }
  double value(double t) const override {
    values_[0] = t;
    return f_(values_);
  }

  // t, then every number param: the names f may use.
  static std::vector<std::string> names(const BlockDef& def) {
    std::vector<std::string> out{"t"};
    for (const auto& p : def.params)
      if (p.is_number()) out.push_back(p.id);
    return out;
  }
  static std::string text_of(const Config& config) {
    auto it = config.find("f");
    return it == config.end() ? "" : it->second;
  }

 private:
  toolkit::Expression f_;
  mutable std::vector<double> values_;
};

}  // namespace

void add_waves(Registry& registry) {
  registry.add(define_block<Clock>("signal", generator_def("clock", "Clock", {}, "t")));

  // The parent: f(t) as an expression `f` (config, or a text param a user edits) over t and
  // the params a definition adds. Not in a palette by itself.
  Template generator;
  generator.name = "signal/generator";
  generator.def = generator_def("generator", "Generator");
  generator.config = {{"f", "0"}};
  generator.extendable = true;
  generator.abstract = true;
  generator.check = [](const BlockDef& def, const Config& config) {
    try {
      const toolkit::Expression f(Expression::text_of(config), Expression::names(def));
    } catch (const ExpressionError& err) {
      throw ExpressionError("f: " + std::string(err.what()));
    }
  };
  // Each definition on it makes its own Template (a copy), so `def` is read at make time
  // from the catalog entry's params: names come from the params given.
  generator.make = [](const Values& params, const Config& config) -> std::unique_ptr<Block> {
    BlockDef def;
    for (const auto& [id, value] : params) def.params.push_back({.id = id});
    try {
      return std::make_unique<Expression>(def, params, config);
    } catch (const ExpressionError& err) {
      throw ExpressionError("f: " + std::string(err.what()));
    }
  };
  registry.add(std::move(generator));
}

}  // namespace paperboat::toolkit::signal
