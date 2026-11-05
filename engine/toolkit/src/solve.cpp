#include "paperboat/toolkit/solve.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace paperboat::toolkit::solve {
namespace {

// f(t, x), checked to be the size of x.
State call(const Deriv& f, double t, const State& x) {
  State dx = f(t, x);
  if (dx.size() != x.size())
    throw std::runtime_error("the equations returned " + std::to_string(dx.size()) + " values for a state of " +
                             std::to_string(x.size()));
  return dx;
}

// x + a·k
State axpy(const State& x, double a, const State& k) {
  State out(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) out[i] = x[i] + a * k[i];
  return out;
}

class Discrete final : public Solver {
 public:
  void step(State& x, double t, double, const Deriv& f) override { x = call(f, t, x); }
  std::string_view name() const override { return "discrete"; }
};

class Euler final : public Solver {
 public:
  void step(State& x, double t, double dt, const Deriv& f) override { x = axpy(x, dt, call(f, t, x)); }
  std::string_view name() const override { return "euler"; }
};

// Heun's method: the trapezoid of the slopes at both ends.
class Rk2 final : public Solver {
 public:
  void step(State& x, double t, double dt, const Deriv& f) override {
    const State k1 = call(f, t, x);
    const State k2 = call(f, t + dt, axpy(x, dt, k1));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += dt / 2 * (k1[i] + k2[i]);
  }
  std::string_view name() const override { return "rk2"; }
};

class Rk4 final : public Solver {
 public:
  void step(State& x, double t, double dt, const Deriv& f) override {
    const State k1 = call(f, t, x);
    const State k2 = call(f, t + dt / 2, axpy(x, dt / 2, k1));
    const State k3 = call(f, t + dt / 2, axpy(x, dt / 2, k2));
    const State k4 = call(f, t + dt, axpy(x, dt, k3));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += dt / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
  }
  std::string_view name() const override { return "rk4"; }
};

// Dormand–Prince 5(4), the method of MATLAB's ode45: a 5th-order step with a 4th-order
// one alongside to estimate the error, sub-stepping from t to t + dt. It keeps the step
// size it settled on for the next clock step.
class Rk45 final : public Solver {
 public:
  explicit Rk45(Options options) : o_(options) {}

  void step(State& x, double t, double dt, const Deriv& f) override {
    static constexpr double c2 = 1. / 5, c3 = 3. / 10, c4 = 4. / 5, c5 = 8. / 9;
    static constexpr double a21 = 1. / 5;
    static constexpr double a31 = 3. / 40, a32 = 9. / 40;
    static constexpr double a41 = 44. / 45, a42 = -56. / 15, a43 = 32. / 9;
    static constexpr double a51 = 19372. / 6561, a52 = -25360. / 2187, a53 = 64448. / 6561, a54 = -212. / 729;
    static constexpr double a61 = 9017. / 3168, a62 = -355. / 33, a63 = 46732. / 5247, a64 = 49. / 176,
                            a65 = -5103. / 18656;
    static constexpr double b1 = 35. / 384, b3 = 500. / 1113, b4 = 125. / 192, b5 = -2187. / 6784, b6 = 11. / 84;
    // The 5th-order solution minus the embedded 4th-order one.
    static constexpr double e1 = 71. / 57600, e3 = -71. / 16695, e4 = 71. / 1920, e5 = -17253. / 339200,
                            e6 = 22. / 525, e7 = -1. / 40;

    const double end = t + dt;
    double h = h_ > 0 ? std::min(h_, dt) : dt;
    const std::size_t n = x.size();
    State k1 = call(f, t, x), y(n), err(n);
    for (int steps = 0;; ++steps) {
      if (steps >= o_.max_steps)
        throw std::runtime_error("rk45 needed more than " + std::to_string(o_.max_steps) +
                                 " sub-steps in one clock step: the system is too stiff, or the tolerance too tight");
      const bool last = t + h >= end - 1e-12 * std::max(1.0, std::abs(end));
      if (last) h = end - t;
      State s(n);
      auto stage = [&](std::initializer_list<std::pair<double, const State*>> terms) {
        for (std::size_t i = 0; i < n; ++i) {
          double sum = 0;
          for (const auto& [a, k] : terms) sum += a * (*k)[i];
          s[i] = x[i] + h * sum;
        }
        return s;
      };
      const State k2 = call(f, t + c2 * h, stage({{a21, &k1}}));
      const State k3 = call(f, t + c3 * h, stage({{a31, &k1}, {a32, &k2}}));
      const State k4 = call(f, t + c4 * h, stage({{a41, &k1}, {a42, &k2}, {a43, &k3}}));
      const State k5 = call(f, t + c5 * h, stage({{a51, &k1}, {a52, &k2}, {a53, &k3}, {a54, &k4}}));
      const State k6 = call(f, t + h, stage({{a61, &k1}, {a62, &k2}, {a63, &k3}, {a64, &k4}, {a65, &k5}}));
      for (std::size_t i = 0; i < n; ++i) y[i] = x[i] + h * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i]);
      const State k7 = call(f, t + h, y);  // the next step's k1 when this one is taken
      double norm = 0;
      for (std::size_t i = 0; i < n; ++i) {
        const double e = h * (e1 * k1[i] + e3 * k3[i] + e4 * k4[i] + e5 * k5[i] + e6 * k6[i] + e7 * k7[i]);
        const double scale = o_.atol + o_.rtol * std::max(std::abs(x[i]), std::abs(y[i]));
        norm += (e / scale) * (e / scale);
      }
      norm = n ? std::sqrt(norm / static_cast<double>(n)) : 0;
      if (!std::isfinite(norm)) throw std::runtime_error("rk45: the equations returned a value that isn't a number");
      // The next h: 0.9 of the size that would just meet the tolerance, within 0.2× to 5×.
      const double grow = norm == 0 ? 5.0 : std::clamp(0.9 * std::pow(norm, -0.2), 0.2, 5.0);
      if (norm <= 1) {
        if (last) {
          x = y;
          return;  // a step cut short to land on `end` says nothing about the size to keep
        }
        t += h;
        x = y;
        k1 = k7;
        h_ = h = h * grow;
      } else {
        h *= std::max(0.2, grow);
      }
      if (h < 1e-14 * std::max(1.0, std::abs(t))) throw std::runtime_error("rk45: the step size fell to nothing (a singularity?)");
    }
  }
  std::string_view name() const override { return "rk45"; }

 private:
  Options o_;
  double h_ = 0;  // the step size it settled on, carried to the next clock step
};

}  // namespace

const std::vector<std::string>& names() {
  static const std::vector<std::string> list = {"rk4", "rk45", "rk2", "euler", "discrete"};
  return list;
}

std::unique_ptr<Solver> make(std::string_view name, Options options) {
  if (name == "rk4") return std::make_unique<Rk4>();
  if (name == "rk45") return std::make_unique<Rk45>(options);
  if (name == "rk2") return std::make_unique<Rk2>();
  if (name == "euler") return std::make_unique<Euler>();
  if (name == "discrete") return std::make_unique<Discrete>();
  std::string known;
  for (const auto& n : names()) known += (known.empty() ? "" : ", ") + n;
  throw std::invalid_argument("no solver \"" + std::string(name) + "\" (" + known + ")");
}

void step(std::string_view solver, State& x, double t, double dt, const Deriv& f, Options options) {
  make(solver, options)->step(x, t, dt, f);
}

ParamDef param(std::string fallback) {
  return {.id = "solver", .type = "enum", .default_text = std::move(fallback), .options = names()};
}

}  // namespace paperboat::toolkit::solve
