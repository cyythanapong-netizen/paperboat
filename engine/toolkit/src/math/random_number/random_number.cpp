// math/random_number.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {
namespace {

class RandomNumber final : public Block {
 public:
  explicit RandomNumber(const Values& p)
      : default_min_(get_or(p, "min", 0.0)),
        default_max_(get_or(p, "max", 1.0)),
        integer_(get_or(p, "integer", 0.0) != 0.0) {
    double seed = get_or(p, "seed", 0.0);
    if (seed != 0.0) {
      rng_.seed(static_cast<std::uint64_t>(seed));
    } else {
      rng_.seed(std::random_device{}());
    }
  }

  Values step(const Context&, const Values& in) override {
    double lo = in.contains("min") ? get_or(in, "min") : default_min_;
    double hi = in.contains("max") ? get_or(in, "max") : default_max_;
    if (lo > hi) std::swap(lo, hi);
    if (lo == hi) return {{"y", lo}};

    if (integer_) {
      long long i_lo = static_cast<long long>(std::ceil(lo));
      long long i_hi = static_cast<long long>(std::floor(hi));
      if (i_lo > i_hi) return {{"y", lo}};
      std::uniform_int_distribution<long long> dist(i_lo, i_hi);
      return {{"y", static_cast<double>(dist(rng_))}};
    } else {
      std::uniform_real_distribution<double> dist(lo, hi);
      return {{"y", dist(rng_)}};
    }
  }

 private:
  double default_min_;
  double default_max_;
  bool integer_;
  std::mt19937_64 rng_;
};

}  // namespace

void add_random_number(Registry& registry) {
  // math/random_number
  auto t = make_template(
      "math", "random_number", "Random Number", {"min", "max"}, {"y"},
      {{.id = "min", .default_value = 0.0, .type = "number"},
       {.id = "max", .default_value = 1.0, .type = "number"},
       {.id = "integer", .default_value = 0.0, .type = "boolean"},
       {.id = "seed", .default_value = 0.0, .type = "number"}},
      [](const Values& p, const Config&) { return std::make_unique<RandomNumber>(p); },
      "Generates random numbers between min and max.", {.compact = true, .glyph = "~U", .value = "{min}…{max}"});
  t.def.cacheable = false;  // a fresh draw each run: never reused from an earlier run
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::math
