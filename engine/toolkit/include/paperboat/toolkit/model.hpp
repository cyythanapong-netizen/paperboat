#pragma once

// A station object that is a system of differential equations: give its state and its
// equations, and a solver advances it each clock step. It still reads (its linked block's
// outputs) and draws itself; it never steps by hand.
//
//   struct Mass final : paperboat::Model {
//     explicit Mass(const paperboat::Values& p)
//         : Model({paperboat::get_or(p, "x0"), 0.0}, paperboat::get_text(p, "solver", "auto")),
//           k_(paperboat::get_or(p, "k")) {}
//     paperboat::solve::State derivatives(double, const paperboat::solve::State& x,
//                                         const paperboat::Values& in) const override {
//       return {x[1], paperboat::get_or(in, "force") - k_ * x[0]};
//     }
//     paperboat::Values read() const override { return {{"x", x()[0]}, {"v", x()[1]}}; }
//     …
//   };

#include <memory>
#include <string_view>

#include "paperboat/toolkit/solve.hpp"
#include "paperboat/toolkit/station.hpp"

namespace paperboat::toolkit {

class Model : public StationObject {
 public:
  // Its initial state, and the solver that advances it (solve::names(); "auto": the run's,
  // with the run's options unless given).
  explicit Model(solve::State initial, std::string_view solver = "auto");
  Model(solve::State initial, std::string_view solver, solve::Options options);

  // dx/dt at time t in state x, under the linked block's inputs (unwired ones absent).
  virtual solve::State derivatives(double t, const solve::State& x, const Values& in) const = 0;

  // Advances the state over the clock step with the solver; the inputs hold through it.
  void step(const Context& ctx, const Values& in) override;

  // The state now.
  const solve::State& x() const { return x_; }

  // Change the solver advancing this model.
  void set_solver(std::string_view solver);
  void set_solver(std::string_view solver, solve::Options options);

  // Start the solver afresh, as after the equations change (a parameter tuned, a switch). The
  // implicit solvers otherwise keep the history of the equations they started with.
  void restart();

 protected:
  // For a model that changes its own state between steps (a bounce, a reset).
  solve::State& state() { return x_; }

 private:
  solve::State x_;
  std::unique_ptr<solve::Solver> solver_;
};

}  // namespace paperboat::toolkit
