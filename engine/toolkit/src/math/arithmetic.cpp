// The math package: pointwise arithmetic and math functions on signals.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "common.hpp"

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

void add_arithmetic(Registry& registry) {
  registry.add(make_template(
      "math", "gain", "Gain", {"u"}, {"y"}, {{"k", 1.0}},
      [](const Values& p, const Config&) {
        const double k = get_or(p, "k");
        return std::make_unique<Stateless>(
            [=](const Context&, const Values& in) { return Values{{"y", k * get_or(in, "u")}}; });
      },
      "Multiplies its input by k.", {.compact = true, .glyph = "gain", .value = "{k}"}));

  // Sum: one input per sign in `signs` ("+-+": a − b + c), ids a, b, c…
  auto sum = make_template(
      "math", "sum", "Sum", {"a", "b"}, {"y"}, {ParamDef{.id = "signs", .type = "text", .default_text = "++"}},
      [](const Values&, const Config& text) {
        const auto signs = parse_signs(text.contains("signs") ? text.at("signs") : "");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          double y = 0;
          for (std::size_t i = 0; i < signs.size(); ++i)
            y += signs[i] * get_or(in, std::string(1, static_cast<char>('a' + i)));
          return Values{{"y", y}};
        });
      },
      "Adds a and b.", {.compact = true, .glyph = "plus"});
  sum.def.ports_from = "signs";
  registry.add(std::move(sum));

  registry.add(make_template(
      "math", "threshold", "Threshold", {"u"}, {"y"}, {{"level", 0.5}},
      [](const Values& p, const Config&) {
        double level = get_or(p, "level");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") > level ? 1.0 : 0.0}};
        });
      },
      "1 above a level, else 0.", {.compact = true, .glyph = "step", .value = "{level}"}));

  registry.add(make_template(
      "math", "saturation", "Saturation", {"u"}, {"y"}, {{"min", -1.0}, {"max", 1.0}},
      [](const Values& p, const Config&) {
        double lo = get_or(p, "min"), hi = get_or(p, "max");
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          return Values{{"y", std::min(std::max(get_or(in, "u"), lo), hi)}};
        });
      },
      "Keeps its input between a minimum and a maximum.", {.compact = true, .glyph = "clamp", .value = "{min}…{max}"}));

  // math/arithmetic
  registry.add(make_template(
      "math", "arithmetic", "Arithmetic", {"a", "b"}, {"y"},
      {{.id = "op", .type = "enum", .default_text = "+", .options = {"+", "-", "*", "/", "%", "^"}}},
      [](const Values& p, const Config&) {
        int op = static_cast<int>(get_or(p, "op", 0.0));
        return std::make_unique<Stateless>([op](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b");
          double y = 0.0;
          switch (op) {
            case 0: y = a + b; break;
            case 1: y = a - b; break;
            case 2: y = a * b; break;
            case 3: y = (b == 0.0) ? 0.0 : (a / b); break;
            case 4: y = (b == 0.0) ? 0.0 : std::fmod(a, b); break;
            case 5: y = std::pow(a, b); break;
            default: y = a + b; break;
          }
          return Values{{"y", y}};
        });
      },
      "Applies an arithmetic operator to a and b.", {.compact = true, .glyph = "{op}", .value = "{op}"}));

  // math/absolute
  registry.add(make_template(
      "math", "absolute", "Absolute", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", std::abs(get_or(in, "u"))}};
        });
      },
      "Computes the absolute value of u.", {.compact = true, .glyph = "abs"}));

  // math/round
  registry.add(make_template(
      "math", "round", "Round", {"u"}, {"y"},
      {{.id = "mode", .type = "enum", .default_text = "round", .options = {"round", "floor", "ceil", "truncate"}}},
      [](const Values& p, const Config&) {
        int mode = static_cast<int>(get_or(p, "mode", 0.0));
        return std::make_unique<Stateless>([mode](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double y = 0.0;
          switch (mode) {
            case 0: y = std::round(u); break;
            case 1: y = std::floor(u); break;
            case 2: y = std::ceil(u); break;
            case 3: y = std::trunc(u); break;
            default: y = std::round(u); break;
          }
          return Values{{"y", y}};
        });
      },
      "Rounds u to an integer using the chosen mode.", {.compact = true, .glyph = "staircase", .value = "{mode}"}));

  // math/min_max
  auto min_max = make_template(
      "math", "min_max", "Min / Max", {"a", "b"}, {"y"},
      {{.id = "mode", .type = "enum", .default_text = "min", .options = {"min", "max"}},
       {.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& p, const Config&) {
        int mode = static_cast<int>(get_or(p, "mode", 0.0));
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 2, 26);
        return std::make_unique<Stateless>([mode, n](const Context&, const Values& in) {
          double res = get_or(in, "a");
          for (int i = 1; i < n; ++i) {
            double v = get_or(in, std::string(1, static_cast<char>('a' + i)));
            res = (mode == 0) ? std::min(res, v) : std::max(res, v);
          }
          return Values{{"y", res}};
        });
      },
      "Selects the minimum or maximum among inputs.", {.compact = true, .glyph = "{mode}", .value = "{mode}"});
  min_max.def.ports_from = "inputs";
  registry.add(std::move(min_max));

  // math/clamp
  registry.add(make_template(
      "math", "clamp", "Clamp", {"u", "min", "max"}, {"y"},
      {{.id = "min", .default_value = 0.0, .type = "number"},
       {.id = "max", .default_value = 1.0, .type = "number"}},
      [](const Values& p, const Config&) {
        double p_lo = get_or(p, "min", 0.0), p_hi = get_or(p, "max", 1.0);
        return std::make_unique<Stateless>([p_lo, p_hi](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double lo = in.contains("min") ? get_or(in, "min") : p_lo;
          double hi = in.contains("max") ? get_or(in, "max") : p_hi;
          if (lo > hi) std::swap(lo, hi);
          return Values{{"y", std::clamp(u, lo, hi)}};
        });
      },
      "Restricts u between min and max.", {.compact = true, .glyph = "clamp", .value = "{min}…{max}"}));

  // math/map_range
  registry.add(make_template(
      "math", "map_range", "Map Range", {"u", "in_min", "in_max", "out_min", "out_max"}, {"y"},
      {{.id = "in_min", .default_value = 0.0, .type = "number"},
       {.id = "in_max", .default_value = 1.0, .type = "number"},
       {.id = "out_min", .default_value = 0.0, .type = "number"},
       {.id = "out_max", .default_value = 100.0, .type = "number"},
       {.id = "clamp", .default_value = 0.0, .type = "boolean"}},
      [](const Values& p, const Config&) {
        double p_in_min = get_or(p, "in_min", 0.0), p_in_max = get_or(p, "in_max", 1.0);
        double p_out_min = get_or(p, "out_min", 0.0), p_out_max = get_or(p, "out_max", 100.0);
        bool clamp_val = get_or(p, "clamp", 0.0) != 0.0;
        return std::make_unique<Stateless>([=](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double in_lo = in.contains("in_min") ? get_or(in, "in_min") : p_in_min;
          double in_hi = in.contains("in_max") ? get_or(in, "in_max") : p_in_max;
          double out_lo = in.contains("out_min") ? get_or(in, "out_min") : p_out_min;
          double out_hi = in.contains("out_max") ? get_or(in, "out_max") : p_out_max;
          if (in_lo == in_hi) return Values{{"y", out_lo}};
          double t = (u - in_lo) / (in_hi - in_lo);
          if (clamp_val) t = std::clamp(t, 0.0, 1.0);
          return Values{{"y", out_lo + t * (out_hi - out_lo)}};
        });
      },
      "Maps u linearly from [in_min, in_max] to [out_min, out_max].",
      {.compact = true, .glyph = "ramp", .value = "{out_min}…{out_max}"}));

  // math/lerp
  registry.add(make_template(
      "math", "lerp", "Lerp", {"a", "b", "t"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b"), t = get_or(in, "t");
          return Values{{"y", a + t * (b - a)}};
        });
      },
      "Linearly interpolates between a and b by factor t.", {.compact = true, .glyph = "lerp"}));

  // math/random_number
  registry.add(make_template(
      "math", "random_number", "Random Number", {"min", "max"}, {"y"},
      {{.id = "min", .default_value = 0.0, .type = "number"},
       {.id = "max", .default_value = 1.0, .type = "number"},
       {.id = "integer", .default_value = 0.0, .type = "boolean"},
       {.id = "seed", .default_value = 0.0, .type = "number"}},
      [](const Values& p, const Config&) { return std::make_unique<RandomNumber>(p); },
      "Generates random numbers between min and max.", {.compact = true, .glyph = "~U", .value = "{min}…{max}"}));

  // math/random_boolean
  registry.add(make_template(
      "math", "random_boolean", "Random Boolean", {"p"}, {"y"},
      {{.id = "p", .default_value = 0.5, .type = "number"},
       {.id = "seed", .default_value = 0.0, .type = "number"}},
      [](const Values& p, const Config&) { return std::make_unique<RandomBoolean>(p); },
      "Generates random booleans with probability p.", {.compact = true, .glyph = "~B", .value = "{p}"}));

  // math/deg_to_rad
  registry.add(make_template(
      "math", "deg_to_rad", "Deg to Rad", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") * (std::numbers::pi / 180.0)}};
        });
      },
      "Converts an angle from degrees to radians.", {.compact = true, .glyph = "°→rad"}));

  // math/rad_to_deg
  registry.add(make_template(
      "math", "rad_to_deg", "Rad to Deg", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          return Values{{"y", get_or(in, "u") * (180.0 / std::numbers::pi)}};
        });
      },
      "Converts an angle from radians to degrees.", {.compact = true, .glyph = "rad→°"}));

  // math/wrap_angle
  registry.add(make_template(
      "math", "wrap_angle", "Wrap Angle", {"u"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double u = get_or(in, "u");
          return Values{{"y", std::atan2(std::sin(u), std::cos(u))}};
        });
      },
      "Wraps an angle in radians into the range [-pi, pi].", {.compact = true, .glyph = "wrap∠"}));

  // math/angle_diff
  registry.add(make_template(
      "math", "angle_diff", "Angle Diff", {"a", "b"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = get_or(in, "a"), b = get_or(in, "b");
          return Values{{"y", std::atan2(std::sin(a - b), std::cos(a - b))}};
        });
      },
      "Computes the shortest signed difference (a - b) normalized to [-pi, pi].", {.compact = true, .glyph = "Δ∠"}));

  // math/atan2
  registry.add(make_template(
      "math", "atan2", "Atan2", {"y", "x"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double y = get_or(in, "y"), x = get_or(in, "x");
          return Values{{"y", std::atan2(y, x)}};
        });
      },
      "Computes the 2-argument arctangent atan2(y, x) in radians.", {.compact = true, .glyph = "atan2"}));

  // math/hypot
  registry.add(make_template(
      "math", "hypot", "Hypot", {"a", "b"}, {"y"}, {},
      [](const Values&, const Config&) {
        return std::make_unique<Stateless>([](const Context&, const Values& in) {
          double a = in.contains("a") ? get_or(in, "a") : get_or(in, "x", 0.0);
          double b = in.contains("b") ? get_or(in, "b") : get_or(in, "y", 0.0);
          return Values{{"y", std::hypot(a, b)}};
        });
      },
      "Computes the Euclidean distance hypot(a, b) = sqrt(a*a + b*b).", {.compact = true, .glyph = "hypot"}));

  // math/deadband
  registry.add(make_template(
      "math", "deadband", "Deadband", {"u"}, {"y"},
      {{.id = "width", .default_value = 0.1, .type = "number"}},
      [](const Values& p, const Config&) {
        double width = get_or(p, "width", 0.1);
        return std::make_unique<Stateless>([width](const Context&, const Values& in) {
          double u = get_or(in, "u");
          double y = 0.0;
          if (std::abs(u) > width) {
            y = (u > 0.0) ? (u - width) : (u + width);
          }
          return Values{{"y", y}};
        });
      },
      "Suppresses signals within [-width, +width].", {.compact = true, .glyph = "deadband", .value = "{width}"}));
}

}  // namespace paperboat::toolkit::math
