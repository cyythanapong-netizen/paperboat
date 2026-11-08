#include "paperboat/toolkit/model.hpp"

#include <utility>

namespace paperboat::toolkit {

Model::Model(solve::State initial, std::string_view solver) : x_(std::move(initial)), solver_(solve::make(solver)) {}

Model::Model(solve::State initial, std::string_view solver, solve::Options options)
    : x_(std::move(initial)), solver_(solve::make(solver, options)) {}

void Model::step(const Context& ctx, const Values& in) {
  solver_->step(x_, ctx.t, ctx.dt, [&](double t, const solve::State& x) { return derivatives(t, x, in); });
}

void Model::set_solver(std::string_view solver) { solver_ = solve::make(solver); }

void Model::set_solver(std::string_view solver, solve::Options options) {
  solver_ = solve::make(solver, options);
}

}  // namespace paperboat::toolkit
