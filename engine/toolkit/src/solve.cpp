#include "paperboat/toolkit/solve.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef PAPERBOAT_WITH_SUNDIALS
#include "implicit.hpp"
#endif

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

// A fixed-step method, taken `sub_steps` times per clock step.
class Fixed : public Solver {
 public:
  explicit Fixed(int sub_steps) : n_(sub_steps) {}
  void step(State& x, double t, double dt, const Deriv& f) final {
    const double h = dt / n_;
    for (int i = 0; i < n_; ++i) once(x, t + i * h, h, f);
  }

 protected:
  virtual void once(State& x, double t, double dt, const Deriv& f) = 0;

 private:
  int n_;
};

class Euler final : public Fixed {
 public:
  using Fixed::Fixed;
  std::string_view name() const override { return "euler"; }

 private:
  void once(State& x, double t, double dt, const Deriv& f) override { x = axpy(x, dt, call(f, t, x)); }
};

// Heun's method: the trapezoid of the slopes at both ends.
class Rk2 final : public Fixed {
 public:
  using Fixed::Fixed;
  std::string_view name() const override { return "rk2"; }

 private:
  void once(State& x, double t, double dt, const Deriv& f) override {
    const State k1 = call(f, t, x);
    const State k2 = call(f, t + dt, axpy(x, dt, k1));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += dt / 2 * (k1[i] + k2[i]);
  }
};

class Rk4 final : public Fixed {
 public:
  using Fixed::Fixed;
  std::string_view name() const override { return "rk4"; }

 private:
  void once(State& x, double t, double dt, const Deriv& f) override {
    const State k1 = call(f, t, x);
    const State k2 = call(f, t + dt / 2, axpy(x, dt / 2, k1));
    const State k3 = call(f, t + dt / 2, axpy(x, dt / 2, k2));
    const State k4 = call(f, t + dt, axpy(x, dt, k3));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += dt / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
  }
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
    const double cap = o_.max_step > 0 ? std::min(o_.max_step, dt) : dt;
    double h = h_ > 0 ? std::min(h_, cap) : cap;
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
        h_ = h = std::min(h * grow, cap);
      } else {
        h *= std::max(0.2, grow);
      }
      if (o_.min_step > 0 && h < o_.min_step)
        throw std::runtime_error("rk45: the step size fell below min_step " + std::to_string(o_.min_step) +
                                 " at t = " + std::to_string(t) + " (a stiff system, or the tolerance too tight)");
      if (h < 1e-14 * std::max(1.0, std::abs(t))) throw std::runtime_error("rk45: the step size fell to nothing (a singularity?)");
    }
  }
  void restart() override { h_ = 0; }
  std::string_view name() const override { return "rk45"; }

 private:
  Options o_;
  double h_ = 0;  // the step size it settled on, carried to the next clock step
};

}  // namespace

const std::vector<std::string>& names() {
  static const std::vector<std::string> list = {"rk4", "rk45", "rk2", "euler", "discrete",
#ifdef PAPERBOAT_WITH_SUNDIALS
                                                "cvode", "ida"
#endif
  };
  return list;
}

namespace {
thread_local Defaults run_defaults;
}

void set_defaults(Defaults d) {
  if (d.name.empty() || d.name == "auto") d.name = "rk4";
  if (std::ranges::find(names(), d.name) == names().end()) make(d.name);  // throws, naming the solvers
  check(d.options);
  run_defaults = std::move(d);
}

const Defaults& defaults() { return run_defaults; }

std::string_view resolve(std::string_view name) {
  return name.empty() || name == "auto" ? std::string_view(run_defaults.name) : name;
}

void check(const Options& o) {
  auto need = [](bool ok, const char* what) {
    if (!ok) throw std::invalid_argument(std::string("solver option ") + what);
  };
  need(std::isfinite(o.rtol) && o.rtol > 0, "rtol must be more than 0");
  need(std::isfinite(o.atol) && o.atol >= 0, "atol must be 0 or more");
  need(std::isfinite(o.min_step) && o.min_step >= 0, "min_step must be 0 or more");
  need(std::isfinite(o.max_step) && o.max_step >= 0, "max_step must be 0 or more");
  need(o.max_step == 0 || o.min_step <= o.max_step, "min_step must not be more than max_step");
  need(o.max_steps >= 1, "max_steps must be 1 or more");
  need(o.sub_steps >= 1 && o.sub_steps <= 10000, "sub_steps must be 1 to 10000");
}

std::unique_ptr<Solver> make(std::string_view name) { return make(name, run_defaults.options); }

std::unique_ptr<Solver> make(std::string_view name, Options options) {
  check(options);
  name = resolve(name);
  if (name == "rk4") return std::make_unique<Rk4>(options.sub_steps);
  if (name == "rk45") return std::make_unique<Rk45>(options);
  if (name == "rk2") return std::make_unique<Rk2>(options.sub_steps);
  if (name == "euler") return std::make_unique<Euler>(options.sub_steps);
  if (name == "discrete") return std::make_unique<Discrete>();
#ifdef PAPERBOAT_WITH_SUNDIALS
  if (name == "cvode") return make_cvode(options);
  if (name == "ida") return make_ida(options);
#else
  if (name == "cvode" || name == "ida")
    throw std::invalid_argument("solver \"" + std::string(name) + "\" needs an engine built with SUNDIALS (PAPERBOAT_WITH_SUNDIALS)");
#endif
  std::string known;
  for (const auto& n : names()) known += (known.empty() ? "" : ", ") + n;
  throw std::invalid_argument("no solver \"" + std::string(name) + "\" (auto, " + known + ")");
}

void step(std::string_view solver, State& x, double t, double dt, const Deriv& f, Options options) {
  make(solver, options)->step(x, t, dt, f);
}

void step(std::string_view solver, State& x, double t, double dt, const Deriv& f) { make(solver)->step(x, t, dt, f); }

}  // namespace paperboat::toolkit::solve
