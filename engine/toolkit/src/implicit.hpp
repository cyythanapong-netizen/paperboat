#pragma once

// The implicit solvers (solve.hpp: "cvode", "ida"), on SUNDIALS. Built with PAPERBOAT_WITH_SUNDIALS.

#include <memory>

#include "paperboat/toolkit/solve.hpp"

namespace paperboat::toolkit::solve {

std::unique_ptr<Solver> make_cvode(const Options& options);
std::unique_ptr<Solver> make_ida(const Options& options);

}  // namespace paperboat::toolkit::solve
