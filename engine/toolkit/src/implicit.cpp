// The implicit solvers on SUNDIALS: "cvode" (CVODE's BDF, x' = f(t, x)) and "ida" (IDA's BDF on the
// residual x' - f(t, x) = 0), each with Newton iterations on a dense Jacobian SUNDIALS estimates by
// difference quotients.
//
// The engine runs on a fixed clock; these integrate inside it with their own variable step and order,
// and stop at each clock step's end (a stop time, so they never step past it: the inputs change
// there). They carry their step size, order and history to the next clock step, and start afresh
// (reinit) when that step doesn't continue the last one: the state was changed between steps (a
// bounce, a reset), the clock jumped, or restart() was called.

#include "implicit.hpp"

#include <cvode/cvode.h>
#include <ida/ida.h>
#include <nvector/nvector_serial.h>
#include <sundials/sundials_context.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <sunmatrix/sunmatrix_dense.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace paperboat::toolkit::solve {
namespace {

// The engine's state is double and its vectors are serial (N_VNew_Serial); a SUNDIALS built
// with another precision would mismatch silently.
static_assert(std::is_same_v<sunrealtype, double>, "SUNDIALS must be built with double precision");

constexpr double kNone = std::numeric_limits<double>::quiet_NaN();

// What both share: SUNDIALS' context, the state and its derivative as vectors, the dense Jacobian
// and its linear solver, and the clock step it reached.
class Implicit : public Solver {
 public:
  explicit Implicit(const Options& options) : o_(options) {
    if (SUNContext_Create(SUN_COMM_NULL, &ctx_) != 0) throw std::runtime_error("SUNDIALS: no context");
    SUNContext_ClearErrHandlers(ctx_);  // not to stderr: kept for the exception
    SUNContext_PushErrHandler(ctx_, &Implicit::on_error, this);
  }
  ~Implicit() override {
    free_vectors();
    SUNContext_Free(&ctx_);
  }
  Implicit(const Implicit&) = delete;
  Implicit& operator=(const Implicit&) = delete;

  void step(State& x, double t, double dt, const Deriv& f) final {
    if (x.empty()) return;  // nothing to integrate
    f_ = &f;
    thrown_ = nullptr;
    error_.clear();
    struct Unset {
      const Deriv*& f;
      ~Unset() { f = nullptr; }
    } unset{f_};
    if (x.size() != n_) {
      forget();
      free_vectors();
      n_ = x.size();
      const auto n = static_cast<sunindextype>(n_);
      y_ = N_VNew_Serial(n, ctx_);
      yp_ = N_VNew_Serial(n, ctx_);
      a_ = SUNDenseMatrix(n, n, ctx_);
      ls_ = y_ && a_ ? SUNLinSol_Dense(y_, a_, ctx_) : nullptr;
      if (!y_ || !yp_ || !a_ || !ls_) {
        free_vectors();
        throw std::runtime_error(std::string(name()) + ": out of memory for a state of " + std::to_string(x.size()));
      }
      t_ = kNone;
    }
    if (!continues(x, t)) {
      std::copy(x.begin(), x.end(), N_VGetArrayPointer(y_));
      check(start(t), "start");
    }
    check(advance(t + dt), "step");
    const sunrealtype* y = N_VGetArrayPointer(y_);
    std::copy(y, y + n_, x.begin());
    last_ = x;
    t_ = t + dt;
  }

  void restart() override { t_ = kNone; }

 protected:
  // Init (first) or reinit SUNDIALS at t from y_; set yp_ first if it needs it. A SUNDIALS flag.
  virtual int start(double t) = 0;
  // Integrate to `end` and stop there, y_ (and yp_) then holding the state at `end`. A SUNDIALS flag.
  virtual int advance(double end) = 0;
  // Free the integrator (the state changed size).
  virtual void forget() = 0;
  virtual std::string flag_name(int flag) const = 0;

  // f(t, y) into out. 0; 1 for a value that isn't a number (SUNDIALS then tries a smaller step);
  // -1 for an exception, kept to rethrow after SUNDIALS returns.
  int eval(double t, N_Vector y, sunrealtype* out) {
    try {
      const sunrealtype* in = N_VGetArrayPointer(y);
      x_.assign(in, in + n_);
      const State dx = (*f_)(t, x_);
      if (dx.size() != n_)
        throw std::runtime_error("the equations returned " + std::to_string(dx.size()) + " values for a state of " +
                                 std::to_string(n_));
      for (std::size_t i = 0; i < n_; ++i) {
        if (!std::isfinite(dx[i])) return 1;
        out[i] = dx[i];
      }
      return 0;
    } catch (...) {
      thrown_ = std::current_exception();
      return -1;
    }
  }

  // Options SUNDIALS takes the same way in both.
  struct Setters {
    int (*tolerances)(void*, sunrealtype, sunrealtype);
    int (*max_steps)(void*, long);
    int (*max_step)(void*, sunrealtype);
    int (*min_step)(void*, sunrealtype);
  };
  void apply(void* mem, const Setters& s) {
    check(s.tolerances(mem, o_.rtol, o_.atol), "rtol, atol");
    check(s.max_steps(mem, static_cast<long>(o_.max_steps)), "max_steps");
    if (o_.max_step > 0) check(s.max_step(mem, o_.max_step), "max_step");
    if (o_.min_step > 0) check(s.min_step(mem, o_.min_step), "min_step");
  }

  // Throws for a failed SUNDIALS call: the equations' own exception, else SUNDIALS' message.
  void check(int flag, const char* what) {
    if (thrown_) {
      t_ = kNone;
      std::rethrow_exception(std::exchange(thrown_, nullptr));
    }
    if (flag >= 0) return;
    t_ = kNone;  // whatever it holds now, the next step starts afresh
    std::string msg = std::string(name()) + ": " + (error_.empty() ? std::string(what) + " failed" : error_) + " (" +
                      flag_name(flag) + ")";
    if (flag == -1)  // CV_TOO_MUCH_WORK, IDA_TOO_MUCH_WORK
      msg += ": more than max_steps " + std::to_string(o_.max_steps) + " internal steps in one clock step";
    throw std::runtime_error(msg);
  }

  SUNContext ctx_ = nullptr;
  N_Vector y_ = nullptr, yp_ = nullptr;
  SUNMatrix a_ = nullptr;
  SUNLinearSolver ls_ = nullptr;
  std::size_t n_ = 0;
  Options o_;

 private:
  // The step starts where the last one ended, from the state it left.
  bool continues(const State& x, double t) const {
    return std::isfinite(t_) && std::abs(t - t_) <= 1e-9 * std::max(1.0, std::abs(t)) && x == last_;
  }

  void free_vectors() {
    if (ls_) SUNLinSolFree(ls_);
    if (a_) SUNMatDestroy(a_);
    if (y_) N_VDestroy(y_);
    if (yp_) N_VDestroy(yp_);
    ls_ = nullptr, a_ = nullptr, y_ = nullptr, yp_ = nullptr;
    n_ = 0;
  }

  static void on_error(int, const char*, const char*, const char* msg, SUNErrCode, void* self, SUNContext) {
    if (msg && *msg) static_cast<Implicit*>(self)->error_ = msg;
  }

  const Deriv* f_ = nullptr;
  State x_;     // f's argument
  State last_;  // the state the last step left
  double t_ = kNone;  // where the last step ended; NaN: the next one starts afresh
  std::string error_;
  std::exception_ptr thrown_;
};

std::string taken_name(char* name) {
  std::string out = name ? name : "?";
  std::free(name);
  return out;
}

class Cvode final : public Implicit {
 public:
  using Implicit::Implicit;
  ~Cvode() override { forget(); }
  std::string_view name() const override { return "cvode"; }

 private:
  static int rhs(sunrealtype t, N_Vector y, N_Vector ydot, void* self) {
    return static_cast<Cvode*>(self)->eval(t, y, N_VGetArrayPointer(ydot));
  }

  int start(double t) override {
    if (mem_) return CVodeReInit(mem_, t, y_);
    mem_ = CVodeCreate(CV_BDF, ctx_);
    if (!mem_) throw std::runtime_error("cvode: out of memory");
    check(CVodeInit(mem_, &Cvode::rhs, t, y_), "init");
    check(CVodeSetUserData(mem_, this), "init");
    check(CVodeSetLinearSolver(mem_, ls_, a_), "the dense linear solver");
    apply(mem_, {CVodeSStolerances, CVodeSetMaxNumSteps, CVodeSetMaxStep, CVodeSetMinStep});
    return 0;
  }

  int advance(double end) override {
    check(CVodeSetStopTime(mem_, end), "stop time");
    sunrealtype reached = 0;
    return CVode(mem_, end, y_, &reached, CV_NORMAL);
  }

  void forget() override {
    if (mem_) CVodeFree(&mem_);
    mem_ = nullptr;
  }

  std::string flag_name(int flag) const override { return taken_name(CVodeGetReturnFlagName(flag)); }

  void* mem_ = nullptr;
};

class Ida final : public Implicit {
 public:
  using Implicit::Implicit;
  ~Ida() override { forget(); }
  std::string_view name() const override { return "ida"; }

 private:
  // F(t, x, x') = x' - f(t, x)
  static int residual(sunrealtype t, N_Vector y, N_Vector yp, N_Vector r, void* self) {
    auto* me = static_cast<Ida*>(self);
    sunrealtype* out = N_VGetArrayPointer(r);
    if (const int flag = me->eval(t, y, out)) return flag;
    const sunrealtype* dx = N_VGetArrayPointer(yp);
    for (std::size_t i = 0; i < me->n_; ++i) out[i] = dx[i] - out[i];
    return 0;
  }

  int start(double t) override {
    // x' = f(t, x): consistent from the start, no initial-condition solve.
    if (const int flag = eval(t, y_, N_VGetArrayPointer(yp_)); flag != 0) {
      if (flag < 0) check(flag, "start");  // rethrows the equations' exception
      restart();
      throw std::runtime_error("ida: the equations returned a value that isn't a number at t = " + std::to_string(t));
    }
    if (mem_) return IDAReInit(mem_, t, y_, yp_);
    mem_ = IDACreate(ctx_);
    if (!mem_) throw std::runtime_error("ida: out of memory");
    check(IDAInit(mem_, &Ida::residual, t, y_, yp_), "init");
    check(IDASetUserData(mem_, this), "init");
    check(IDASetLinearSolver(mem_, ls_, a_), "the dense linear solver");
    apply(mem_, {IDASStolerances, IDASetMaxNumSteps, IDASetMaxStep, IDASetMinStep});
    return 0;
  }

  int advance(double end) override {
    check(IDASetStopTime(mem_, end), "stop time");
    sunrealtype reached = 0;
    return IDASolve(mem_, end, &reached, y_, yp_, IDA_NORMAL);
  }

  void forget() override {
    if (mem_) IDAFree(&mem_);
    mem_ = nullptr;
  }

  std::string flag_name(int flag) const override { return taken_name(IDAGetReturnFlagName(flag)); }

  void* mem_ = nullptr;
};

}  // namespace

std::unique_ptr<Solver> make_cvode(const Options& options) { return std::make_unique<Cvode>(options); }
std::unique_ptr<Solver> make_ida(const Options& options) { return std::make_unique<Ida>(options); }

}  // namespace paperboat::toolkit::solve
