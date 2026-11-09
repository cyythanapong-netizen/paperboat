#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>

#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::WithinAbs;

namespace {

std::unique_ptr<Block> make(const std::string& name, const Values& params) {
  const Template* t = builtins().find(name);
  REQUIRE(t != nullptr);
  return t->make(params, t->config);
}

double y(Block& b, double u) {
  return get_or(b.step(Context{}, {{"u", u}}), "y");
}

}  // namespace

TEST_CASE("Boost.Math blocks") {
  SECTION("math/erf") {
    auto b = make("math/erf", {});
    CHECK_THAT(y(*b, 1.0), WithinAbs(0.8427007929497149, 1e-12));
    CHECK_THAT(y(*b, 0.0), WithinAbs(0.0, 1e-15));
  }

  SECTION("math/gamma") {
    CHECK_THAT(y(*make("math/gamma", {{"mode", 0.0}}), 5.0), WithinAbs(24.0, 1e-10));
    CHECK_THAT(y(*make("math/gamma", {{"mode", 1.0}}), 5.0), WithinAbs(std::log(24.0), 1e-12));
    CHECK_THAT(y(*make("math/gamma", {{"mode", 2.0}}), 1.0), WithinAbs(-0.5772156649015329, 1e-12));
  }

  SECTION("math/bessel") {
    CHECK_THAT(y(*make("math/bessel", {{"order", 0.0}, {"kind", 0.0}}), 1.0),
               WithinAbs(0.7651976865579666, 1e-12));  // J0(1)
    CHECK_THAT(y(*make("math/bessel", {{"order", 1.0}, {"kind", 2.0}}), 2.0),
               WithinAbs(1.5906368546373291, 1e-10));  // I1(2)
  }

  SECTION("math/distribution") {
    // normal: a = mean, b = standard deviation
    CHECK_THAT(y(*make("math/distribution", {{"family", 0.0}, {"function", 1.0}, {"a", 0.0}, {"b", 1.0}}), 1.96),
               WithinAbs(0.9750021048517795, 1e-12));  // cdf
    CHECK_THAT(y(*make("math/distribution", {{"family", 0.0}, {"function", 0.0}, {"a", 0.0}, {"b", 1.0}}), 0.0),
               WithinAbs(0.3989422804014327, 1e-12));  // pdf
    CHECK_THAT(y(*make("math/distribution", {{"family", 0.0}, {"function", 2.0}, {"a", 0.0}, {"b", 1.0}}), 0.975),
               WithinAbs(1.959963984540054, 1e-9));  // quantile
  }
}

TEST_CASE("signal/fft names the strongest frequency") {
  constexpr std::size_t kSamples = 256;
  constexpr double kRate = 64.0;
  constexpr double kTone = 5.0;
  auto b = make("signal/fft", {{"size", 2.0}, {"window", 0.0}, {"rate", kRate}});  // 256, rectangular
  Context ctx;
  ctx.dt = 1.0 / kRate;

  Values out;
  for (std::size_t i = 0; i < kSamples; ++i) {
    const double u = std::sin(2.0 * std::numbers::pi * kTone * static_cast<double>(i) / kRate);
    out = b->step(ctx, {{"u", u}});
  }
  CHECK_THAT(get_or(out, "peak_hz"), WithinAbs(kTone, 1e-9));
  const Vector* magnitude = get_vector(out, "magnitude");
  REQUIRE(magnitude != nullptr);
  CHECK(magnitude->size() == kSamples / 2);
  const std::size_t bin = static_cast<std::size_t>(kTone * kSamples / kRate);
  CHECK_THAT((*magnitude)[bin], WithinAbs(1.0, 1e-9));
  CHECK_THAT((*magnitude)[bin + 1], WithinAbs(0.0, 1e-9));
}
