// math/random_boolean.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"
#include "math/arithmetic_shared.hpp"

namespace paperboat::toolkit::math {
namespace {

class RandomBoolean final : public Block {
 public:
  explicit RandomBoolean(const Values& p) : default_p_(get_or(p, "p", 0.5)) {
    double seed = get_or(p, "seed", 0.0);
    if (seed != 0.0) {
      rng_.seed(static_cast<std::uint64_t>(seed));
    } else {
      rng_.seed(std::random_device{}());
    }
  }

  Values step(const Context&, const Values& in) override {
    double prob = std::clamp(in.contains("p") ? get_or(in, "p") : default_p_, 0.0, 1.0);
    std::bernoulli_distribution dist(prob);
    return {{"y", dist(rng_) ? 1.0 : 0.0}};
  }

 private:
  double default_p_;
  std::mt19937_64 rng_;
};

}  // namespace

void add_random_boolean(Registry& registry) {
  // math/random_boolean
  registry.add(make_template(
      "math", "random_boolean", "Random Boolean", {"p"}, {"y"},
      {{.id = "p", .default_value = 0.5, .type = "number"},
       {.id = "seed", .default_value = 0.0, .type = "number"}},
      [](const Values& p, const Config&) { return std::make_unique<RandomBoolean>(p); },
      "Generates random booleans with probability p.", {.compact = true, .glyph = "~B", .value = "{p}"}));
}

}  // namespace paperboat::toolkit::math
