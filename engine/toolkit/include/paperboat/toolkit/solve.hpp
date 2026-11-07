#pragma once

// Solvers: advance a state by one clock step, so domain code writes only its equations.
//
//   x' = f(t, x)       a continuous system: "euler", "rk2" (Heun), "rk4" (classic), or "rk45"
//                      (Dormand–Prince, adaptive: it sub-steps inside the clock step to meet
//                      its tolerances, so signals still arrive on the clock)
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

// rk45's error control (the others ignore it).
struct Options {
  double rtol = 1e-6;       // relative tolerance, per component
  double atol = 1e-9;       // absolute tolerance, per component
  int max_steps = 100000;   // sub-steps in one clock step before it gives up
};

class Solver {
 public:
  virtual ~Solver() = default;
  // Advance `x` from `t` to `t + dt`. Throws std::runtime_error when f returns a state of
  // another size, or rk45 can't meet its tolerances in max_steps sub-steps.
  virtual void step(State& x, double t, double dt, const Deriv& f) = 0;

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

// The solvers by name, the default first: rk4, rk45, rk2, euler, discrete.
const std::vector<std::string>& names();

// A solver by name. Throws std::invalid_argument naming the ones there are.
std::unique_ptr<Solver> make(std::string_view name, Options options = {});

// One step with a fresh solver of that name (rk45 then starts its sub-steps at dt).
void step(std::string_view solver, State& x, double t, double dt, const Deriv& f, Options options = {});

template <std::size_t N>
void step(std::string_view solver, std::array<double, N>& x, double t, double dt,
          const std::function<std::array<double, N>(double, const std::array<double, N>&)>& f, Options options = {}) {
  make(solver, options)->step<N>(x, t, dt, f);
}

// An enum param or prop `solver` (choose one in the UI), defaulting to `fallback`.
ParamDef param(std::string fallback = "rk4");

}  // namespace paperboat::toolkit::solve
