#pragma once

// Fixed-step integrators for domain plants (a cart-pole, a mass on a spring). The
// plant owns its state and equations; these only advance x' = f(t, x) by one step.

#include <array>
#include <cstddef>

namespace paperboat::toolkit::control {

template <std::size_t N>
using State = std::array<double, N>;

namespace detail {
template <std::size_t N>
State<N> axpy(const State<N>& x, double a, const State<N>& y) {  // x + a·y
  State<N> out{};
  for (std::size_t i = 0; i < N; ++i) out[i] = x[i] + a * y[i];
  return out;
}
}  // namespace detail

// f(t, x) returns dx/dt as a State<N>.
template <std::size_t N, class Deriv>
State<N> euler_step(const State<N>& x, double t, double dt, Deriv f) {
  return detail::axpy(x, dt, f(t, x));
}

// Classic 4th-order Runge-Kutta.
template <std::size_t N, class Deriv>
State<N> rk4_step(const State<N>& x, double t, double dt, Deriv f) {
  State<N> k1 = f(t, x);
  State<N> k2 = f(t + dt / 2, detail::axpy(x, dt / 2, k1));
  State<N> k3 = f(t + dt / 2, detail::axpy(x, dt / 2, k2));
  State<N> k4 = f(t + dt, detail::axpy(x, dt, k3));
  State<N> out{};
  for (std::size_t i = 0; i < N; ++i) out[i] = x[i] + dt / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
  return out;
}

}  // namespace paperboat::toolkit::control
