#include "paperboat/toolkit/bases.hpp"

#include <algorithm>

namespace paperboat::toolkit {

Values Generator::step(const Context& ctx, const Values& in) {
  const double t = in.contains("t") ? get_or(in, "t") : ctx.t;
  return {{output_, value(t)}};
}

BlockDef generator_def(std::string type, std::string label, std::vector<ParamDef> params, std::string output) {
  return {.type = std::move(type),
          .label = std::move(label),
          .kind = Kind::Function,
          .inputs = {{"t"}},
          .outputs = {{std::move(output)}},
          .params = std::move(params)};
}

void Timed::enter(const Context& ctx, const Values& in) {
  done_ = 0;
  p_ = 0;
  first_ = true;
  begin(ctx, in);
}

std::string Timed::tick(const Context& ctx, const Values& in) {
  const double seconds = std::max(seconds_, ctx.dt);
  if (done_ >= seconds - ctx.dt / 2) {
    go(ctx, 1.0);
    return "next";
  }
  done_ += ctx.dt;
  double target = std::min(done_ / seconds, 1.0);
  // A curve's first frame would read what it last said, before this step began: stay at 0.
  if (in.contains("curve")) target = first_ ? 0.0 : std::clamp(get_or(in, "curve"), 0.0, 1.0);
  first_ = false;
  go(ctx, target);
  return "";
}

void Timed::go(const Context& ctx, double p) {
  if (p != p_) advance(ctx, p_, p);
  p_ = p;
}

Values Timed::outputs() const { return {{"progress", seconds_ > 0 ? std::min(done_ / seconds_, 1.0) : 1.0}}; }

BlockDef timed_def(std::string type, std::string label, std::vector<ParamDef> params, Kind kind) {
  if (std::ranges::find(params, std::string("seconds"), &ParamDef::id) == params.end())
    params.push_back({.id = "seconds", .default_value = 1});
  return {.type = std::move(type),
          .label = std::move(label),
          .kind = kind,
          .inputs = {{"in", "step"}, {"curve"}},
          .outputs = {{"next", "step"}, {"progress"}},
          .params = std::move(params)};
}

}  // namespace paperboat::toolkit
