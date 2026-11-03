#pragma once

// Base classes for kinds of block: derive from one and implement only what is particular.
// A base does the kind's work once, in C++ (Python domain code gets the same through the
// bindings: paperboat.Generator, paperboat.Timed).
//
//   Generator  a signal f(t): t is the block's input `t` when wired (a step's `progress`,
//              say), else the run clock. The toolkit's signal/generator is one, whose f is
//              an expression, so Sine, Ramp, Ease… are TOML definitions on it.
//   Timed      a step that does its work over `seconds`, following its optional `curve`
//              input (0 to 1) when wired, evenly otherwise, and reports `progress`.

#include <string>
#include <vector>

#include "paperboat/toolkit/block.hpp"

namespace paperboat::toolkit {

class Generator : public Block {
 public:
  explicit Generator(std::string output = "y") : output_(std::move(output)) {}
  // The signal at `t`.
  virtual double value(double t) const = 0;
  Values step(const Context& ctx, const Values& in) final;

 private:
  std::string output_;
};

// A generator's definition: input `t` (optional), output `output`, these params.
BlockDef generator_def(std::string type, std::string label, std::vector<ParamDef> params = {}, std::string output = "y");

class Timed : public Step {
 public:
  explicit Timed(double seconds) : seconds_(seconds) {}
  void enter(const Context& ctx, const Values& in) final;
  // Each clock step: the work moves to the curve's value (clamped to 0–1; the first
  // frame stays at 0) or, unwired, to the share of `seconds` gone. The last frame goes to 1
  // and leaves by `next`. A steady 0.5 jumps to half at once, holds, and ends at 1.
  std::string tick(const Context& ctx, const Values& in) final;
  // `progress`: the share of `seconds` gone (not the curve): feed it to a curve.
  Values outputs() const final;

 protected:
  // A path arrived (before the first frame).
  virtual void begin(const Context& /*ctx*/, const Values& /*in*/) {}
  // Move the work from progress `from` to `to` (0 to 1; may go back for a curve that does).
  virtual void advance(const Context& ctx, double from, double to) = 0;

 private:
  void go(const Context& ctx, double p);
  double seconds_;
  double done_ = 0;  // s since the path arrived
  double p_ = 0;     // where the work is
  bool first_ = true;
};

// A timed step's definition: ports in, curve → next, progress; params `seconds` (1, if
// `params` doesn't have it) and these.
BlockDef timed_def(std::string type, std::string label, std::vector<ParamDef> params = {}, Kind kind = Kind::Function);

}  // namespace paperboat::toolkit
