#pragma once

// Solvers: advance a state by one clock step, so domain code writes only its equations.
//
//   x' = f(t, x)       a continuous system: "euler", "rk2" (Heun), "rk4" (classic), or "rk45"
//                      (Dormand–Prince, adaptive: it sub-steps inside the clock step to meet
//                      its tolerances, so signals still arrive on the clock)
//                      or, for stiff systems, "cvode" (SUNDIALS CVODE: BDF with Newton on a dense
//                      Jacobian) or "ida" (SUNDIALS IDA on the residual x' - f(t, x) = 0). The
//                      implicit two pick their own step and order inside the clock step, stop at its
//                      end, and carry their history to the next one (names() has them when the
//                      engine is built with SUNDIALS, PAPERBOAT_WITH_SUNDIALS)
//   x[k+1] = f(t, x)   a difference equation: "discrete"
//
// The state is any size, known at run time. A station object that is a system of ODEs
// derives from Model (model.hpp) and never calls a solver itself.
//
//   paperboat::solve::State x = {1.0, 0.0};                     // position, velocity
//   auto rk4 = paperboat::solve::make("rk4");
//   rk4->step(x, t, dt, [&](double, const paperboat::solve::State& s) {
//     return paperboat::solve::State{s[1], -k / m * s[0]};
//   });

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/block.hpp"

namespace paperboat::toolkit::solve {

using State = std::vector<double>;
// dx/dt at (t, x); for "discrete", the next state.
using Deriv = std::function<State(double t, const State& x)>;

struct Options {
  // Error control (rk45, cvode, ida)
  double rtol = 1e-6;       // relative tolerance, per component
  double atol = 1e-9;       // absolute tolerance, per component
  double min_step = 0.0;    // smallest sub-step before it gives up (0: automatic)
  double max_step = 0.0;    // largest sub-step (0: the clock step)
  int max_steps = 100000;   // sub-steps in one clock step before it gives up
  // Fixed sub-stepping (rk4, rk2, euler): steps of dt / sub_steps per clock step
  int sub_steps = 1;
  bool operator==(const Options&) const = default;
};

// The run's solver and options: graph.run's settings (the Run menu) over the backend's.
// A solver named "auto" (or "") is this one; options left out are these. Set per thread: a
// run sets them on its own before it makes its blocks and objects.
struct Defaults {
  std::string name = "rk4";
  Options options;
};
void set_defaults(Defaults defaults);
const Defaults& defaults();

class Solver {
 public:
  virtual ~Solver() = default;
  // Advance `x` from `t` to `t + dt`. Throws std::runtime_error when f returns a state of
  // another size, or rk45, cvode or ida can't meet its tolerances in max_steps sub-steps.
  virtual void step(State& x, double t, double dt, const Deriv& f) = 0;

  // Forget what it carries between clock steps (rk45's step size, cvode's and ida's history):
  // call it after the equations change (a parameter tuned, a switch). cvode and ida also start
  // afresh on their own when a step doesn't begin where the last one ended, from the state it
  // left (a bounce, a reset, a new run).
  virtual void restart() {}

  // Fixed-size overload: state as std::array<double, N>.
  template <std::size_t N>
  void step(std::array<double, N>& x, double t, double dt,
            const std::function<std::array<double, N>(double, const std::array<double, N>&)>& f) {
    State s(x.begin(), x.end());
    step(s, t, dt, [&](double at, const State& st) {
      std::array<double, N> arr;
      std::copy_n(st.begin(), N, arr.begin());
      auto res = f(at, arr);
      return State(res.begin(), res.end());
    });
    std::copy_n(s.begin(), N, x.begin());
  }

  virtual std::string_view name() const = 0;
};

// The solvers by name, the default first: rk4, rk45, rk2, euler, discrete, then cvode and ida
// when the engine has them (built with SUNDIALS).
const std::vector<std::string>& names();

// A solver by name ("auto": the run's), with the run's options unless given. Throws
// std::invalid_argument naming the ones there are, or for options out of range.
std::unique_ptr<Solver> make(std::string_view name, Options options);
std::unique_ptr<Solver> make(std::string_view name = "auto");

// "auto" (or "") as the run's solver name; any other name as it is.
std::string_view resolve(std::string_view name);

// Throws std::invalid_argument for an option out of range, naming it.
void check(const Options& options);

// One step with a fresh solver of that name (rk45 then starts its sub-steps at dt; cvode and
// ida start afresh, so a solver kept between steps is much faster for them).
void step(std::string_view solver, State& x, double t, double dt, const Deriv& f, Options options);
void step(std::string_view solver, State& x, double t, double dt, const Deriv& f);

template <std::size_t N>
void step(std::string_view solver, std::array<double, N>& x, double t, double dt,
          const std::function<std::array<double, N>(double, const std::array<double, N>&)>& f) {
  make(solver)->step<N>(x, t, dt, f);
}
template <std::size_t N>
void step(std::string_view solver, std::array<double, N>& x, double t, double dt,
          const std::function<std::array<double, N>(double, const std::array<double, N>&)>& f, Options options) {
  make(solver, options)->step<N>(x, t, dt, f);
}

// An enum param or prop `solver` (choose one in the UI): "auto" (the run's) or one of names().
ParamDef param(std::string fallback = "auto");

}  // namespace paperboat::toolkit::solve
