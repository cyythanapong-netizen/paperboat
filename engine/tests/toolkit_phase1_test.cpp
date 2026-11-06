#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <string>

#include "paperboat/toolkit/block.hpp"
#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::WithinAbs;

TEST_CASE("Base Toolkit: Type and Conversion") {
  Registry reg = builtins();

  SECTION("base/to_number") {
    const auto* t = reg.find("base/to_number");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "→#");
    CHECK(t->def.view.value == "{fallback}");

    auto b = t->make({{"fallback", 99.0}}, {});
    Context ctx;
    // double
    CHECK(get_or(b->step(ctx, {{"in", 42.5}}), "y") == 42.5);
    // string number
    CHECK(get_or(b->step(ctx, {{"in", std::string(" -12.34 ")}}), "y") == -12.34);
    // string boolean
    CHECK(get_or(b->step(ctx, {{"in", std::string("true")}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("false")}}), "y") == 0.0);
    // invalid string falls back to param
    CHECK(get_or(b->step(ctx, {{"in", std::string("not_a_number")}}), "y") == 99.0);
    // wired fallback overrides param
    CHECK(get_or(b->step(ctx, {{"in", std::string("bad")}, {"fallback", 7.0}}), "y") == 7.0);
    // unconnected falls back
    CHECK(get_or(b->step(ctx, {}), "y") == 99.0);
  }

  SECTION("base/to_text") {
    const auto* t = reg.find("base/to_text");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "→T");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"in", 42.0}}), "y") == "42");
    CHECK(get_text(b->step(ctx, {{"in", 3.14}}), "y") == "3.14");
    CHECK(get_text(b->step(ctx, {{"in", std::string("hello world")}}), "y") == "hello world");
    CHECK(get_text(b->step(ctx, {{"in", Vector{1.0, 2.5}}}), "y") == "[1, 2.5]");
    CHECK(get_text(b->step(ctx, {}), "y") == "");
  }

  SECTION("base/to_boolean") {
    const auto* t = reg.find("base/to_boolean");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "?0/1");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"in", 1.0}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", -3.5}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", 0.0}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("true")}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("false")}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("0")}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("off")}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"in", std::string("anything else")}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", Vector{1.0}}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"in", Vector{}}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0);
  }

  SECTION("base/type_of") {
    const auto* t = reg.find("base/type_of");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "type");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"in", 1.0}}), "y") == "number");
    CHECK(get_text(b->step(ctx, {{"in", std::string("hi")}}), "y") == "text");
    CHECK(get_text(b->step(ctx, {{"in", Vector{1.0}}}), "y") == "vector");
    CHECK(get_text(b->step(ctx, {{"in", Table{{{1.0}}}}}), "y") == "table");
    CHECK(get_text(b->step(ctx, {}), "y") == "empty");
  }
}

TEST_CASE("Base Toolkit: Logic and Comparison") {
  Registry reg = builtins();

  SECTION("base/xor") {
    const auto* t = reg.find("base/xor");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "=1");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"a", 1.0}, {"b", 0.0}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"a", 0.0}, {"b", 1.0}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"a", 1.0}, {"b", 1.0}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"a", 0.0}, {"b", 0.0}}), "y") == 0.0);

    auto step_b = t->make_step({}, {});
    CHECK(step_b->tick(ctx, {{"a", 1.0}, {"b", 0.0}}) == "yes");
    CHECK(step_b->tick(ctx, {{"a", 1.0}, {"b", 1.0}}) == "no");
  }

  SECTION("base/in_range") {
    const auto* t = reg.find("base/in_range");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "window");
    CHECK(t->def.view.value == "{min}…{max}");

    auto b = t->make({{"min", 0.0}, {"max", 10.0}, {"inclusive_min", 1.0}, {"inclusive_max", 0.0}}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"u", 0.0}}), "y") == 1.0);  // inclusive min
    CHECK(get_or(b->step(ctx, {{"u", 5.0}}), "y") == 1.0);
    CHECK(get_or(b->step(ctx, {{"u", 10.0}}), "y") == 0.0); // exclusive max
    CHECK(get_or(b->step(ctx, {{"u", -1.0}}), "y") == 0.0);

    // Wired override
    CHECK(get_or(b->step(ctx, {{"u", 15.0}, {"max", 20.0}}), "y") == 1.0);
  }

  SECTION("multi-input base/and and base/or") {
    const auto* and_t = reg.find("base/and");
    REQUIRE(and_t != nullptr);
    auto and_b = and_t->make({{"inputs", 3.0}}, {});
    Context ctx;
    CHECK(get_or(and_b->step(ctx, {{"a", 1.0}, {"b", 1.0}, {"c", 1.0}}), "y") == 1.0);
    CHECK(get_or(and_b->step(ctx, {{"a", 1.0}, {"b", 1.0}, {"c", 0.0}}), "y") == 0.0);

    const auto* or_t = reg.find("base/or");
    REQUIRE(or_t != nullptr);
    auto or_b = or_t->make({{"inputs", 3.0}}, {});
    CHECK(get_or(or_b->step(ctx, {{"a", 0.0}, {"b", 0.0}, {"c", 1.0}}), "y") == 1.0);
    CHECK(get_or(or_b->step(ctx, {{"a", 0.0}, {"b", 0.0}, {"c", 0.0}}), "y") == 0.0);
  }
}

TEST_CASE("Base Toolkit: Time and Events") {
  Registry reg = builtins();

  SECTION("base/now") {
    const auto* t = reg.find("base/now");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "now");

    auto b = t->make({}, {});
    Context ctx{.t = 1.25};
    CHECK(get_or(b->step(ctx, {}), "y") == 1250.0);
  }

  SECTION("base/interval") {
    const auto* t = reg.find("base/interval");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "pulse");
    CHECK(t->def.view.value == "{interval} ms");

    auto b = t->make({{"interval", 100.0}}, {}); // 0.1s
    Context ctx{.t = 0.0, .dt = 0.05};
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0); // t=0.0
    ctx.t = 0.05;
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0); // t=0.05
    ctx.t = 0.10;
    CHECK(get_or(b->step(ctx, {}), "y") == 1.0); // t=0.10: pulse!
    ctx.t = 0.15;
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0);
    ctx.t = 0.20;
    CHECK(get_or(b->step(ctx, {}), "y") == 1.0); // t=0.20: pulse!

    // Stop signal
    ctx.t = 0.25;
    b->step(ctx, {{"stop", 1.0}});
    ctx.t = 0.30;
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0); // stopped
  }

  SECTION("base/timeout") {
    const auto* t = reg.find("base/timeout");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "timer");
    CHECK(t->def.view.value == "{delay} ms");

    auto b = t->make({{"delay", 200.0}}, {}); // 0.2s
    Context ctx{.t = 0.0, .dt = 0.05};
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0);
    ctx.t = 0.10;
    CHECK(get_or(b->step(ctx, {}), "y") == 0.0);
    ctx.t = 0.20;
    CHECK(get_or(b->step(ctx, {}), "y") == 1.0); // elapsed
    ctx.t = 0.25;
    CHECK(get_or(b->step(ctx, {}), "y") == 1.0); // stays 1

    // Reset
    ctx.t = 0.30;
    CHECK(get_or(b->step(ctx, {{"reset", 1.0}}), "y") == 0.0); // reset
  }

  SECTION("base/delay") {
    const auto* t = reg.find("base/delay");
    REQUIRE(t != nullptr);
    CHECK(t->def.feedthrough == false);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "z⁻¹");

    auto b = t->make({{"delay", 100.0}}, {}); // 0.1s
    Context ctx{.t = 0.0, .dt = 0.05};
    CHECK(get_or(b->step(ctx, {{"in", 10.0}}), "y") == 0.0); // initial
    ctx.t = 0.05;
    CHECK(get_or(b->step(ctx, {{"in", 20.0}}), "y") == 0.0);
    ctx.t = 0.10;
    CHECK(get_or(b->step(ctx, {{"in", 30.0}}), "y") == 10.0); // matured sample
    ctx.t = 0.15;
    CHECK(get_or(b->step(ctx, {}), "y") == 20.0);
  }

  SECTION("base/key_hold") {
    const auto* t = reg.find("base/key_hold");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "⌨");
    CHECK(t->def.view.value == "{key}");

    auto b = t->make({}, {{"key", "KeyA"}});
    KeySet keys{"KeyA"};
    Context ctx{.dt = 0.1, .keys_down = &keys};
    auto out1 = b->step(ctx, {});
    CHECK(get_or(out1, "held") == 1.0);
    CHECK_THAT(get_or(out1, "duration"), WithinAbs(0.1, 1e-9));

    auto out2 = b->step(ctx, {});
    CHECK(get_or(out2, "held") == 1.0);
    CHECK_THAT(get_or(out2, "duration"), WithinAbs(0.2, 1e-9));

    // Released
    KeySet empty_keys{};
    ctx.keys_down = &empty_keys;
    auto out3 = b->step(ctx, {});
    CHECK(get_or(out3, "held") == 0.0);
    CHECK(get_or(out3, "duration") == 0.0);
  }
}

TEST_CASE("Math Toolkit: Additions") {
  Registry reg = builtins();

  SECTION("math/arithmetic") {
    const auto* t = reg.find("math/arithmetic");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "{op}");

    Context ctx;
    // Add
    auto add_b = t->make({{"op", 0.0}}, {});
    CHECK(get_or(add_b->step(ctx, {{"a", 3.0}, {"b", 4.0}}), "y") == 7.0);

    // Div with safe 0
    auto div_b = t->make({{"op", 3.0}}, {});
    CHECK(get_or(div_b->step(ctx, {{"a", 6.0}, {"b", 2.0}}), "y") == 3.0);
    CHECK(get_or(div_b->step(ctx, {{"a", 6.0}, {"b", 0.0}}), "y") == 0.0);

    // Pow
    auto pow_b = t->make({{"op", 5.0}}, {});
    CHECK(get_or(pow_b->step(ctx, {{"a", 2.0}, {"b", 3.0}}), "y") == 8.0);
  }

  SECTION("math/absolute") {
    const auto* t = reg.find("math/absolute");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "abs");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"u", -42.5}}), "y") == 42.5);
    CHECK(get_or(b->step(ctx, {{"u", 10.0}}), "y") == 10.0);
  }

  SECTION("math/round") {
    const auto* t = reg.find("math/round");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "staircase");

    Context ctx;
    auto round_b = t->make({{"mode", 0.0}}, {}); // round
    CHECK(get_or(round_b->step(ctx, {{"u", 2.6}}), "y") == 3.0);
    CHECK(get_or(round_b->step(ctx, {{"u", 2.4}}), "y") == 2.0);

    auto floor_b = t->make({{"mode", 1.0}}, {}); // floor
    CHECK(get_or(floor_b->step(ctx, {{"u", 2.9}}), "y") == 2.0);

    auto ceil_b = t->make({{"mode", 2.0}}, {}); // ceil
    CHECK(get_or(ceil_b->step(ctx, {{"u", 2.1}}), "y") == 3.0);

    auto trunc_b = t->make({{"mode", 3.0}}, {}); // trunc
    CHECK(get_or(trunc_b->step(ctx, {{"u", -2.9}}), "y") == -2.0);
  }

  SECTION("math/min_max") {
    const auto* t = reg.find("math/min_max");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.ports_from == "inputs");

    Context ctx;
    auto min_b = t->make({{"mode", 0.0}, {"inputs", 3.0}}, {});
    CHECK(get_or(min_b->step(ctx, {{"a", 5.0}, {"b", 2.0}, {"c", 9.0}}), "y") == 2.0);

    auto max_b = t->make({{"mode", 1.0}, {"inputs", 3.0}}, {});
    CHECK(get_or(max_b->step(ctx, {{"a", 5.0}, {"b", 2.0}, {"c", 9.0}}), "y") == 9.0);
  }

  SECTION("math/clamp") {
    const auto* t = reg.find("math/clamp");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "clamp");

    auto b = t->make({{"min", 0.0}, {"max", 10.0}}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"u", -5.0}}), "y") == 0.0);
    CHECK(get_or(b->step(ctx, {{"u", 5.0}}), "y") == 5.0);
    CHECK(get_or(b->step(ctx, {{"u", 15.0}}), "y") == 10.0);
  }

  SECTION("math/map_range") {
    const auto* t = reg.find("math/map_range");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "ramp");

    auto b = t->make({{"in_min", 0.0}, {"in_max", 10.0}, {"out_min", 0.0}, {"out_max", 100.0}, {"clamp", 1.0}}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"u", 5.0}}), "y") == 50.0);
    CHECK(get_or(b->step(ctx, {{"u", 15.0}}), "y") == 100.0); // clamped
    CHECK(get_or(b->step(ctx, {{"u", -5.0}}), "y") == 0.0);   // clamped
  }

  SECTION("math/lerp") {
    const auto* t = reg.find("math/lerp");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "lerp");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"a", 10.0}, {"b", 20.0}, {"t", 0.25}}), "y") == 12.5);
  }

  SECTION("math/random_number and random_boolean reproducibility") {
    const auto* num_t = reg.find("math/random_number");
    REQUIRE(num_t != nullptr);
    auto n1 = num_t->make({{"seed", 12345.0}, {"min", 0.0}, {"max", 100.0}}, {});
    auto n2 = num_t->make({{"seed", 12345.0}, {"min", 0.0}, {"max", 100.0}}, {});
    Context ctx;
    double v1 = get_or(n1->step(ctx, {}), "y");
    double v2 = get_or(n2->step(ctx, {}), "y");
    CHECK(v1 == v2);

    const auto* bool_t = reg.find("math/random_boolean");
    REQUIRE(bool_t != nullptr);
    auto b1 = bool_t->make({{"seed", 999.0}, {"p", 0.5}}, {});
    auto b2 = bool_t->make({{"seed", 999.0}, {"p", 0.5}}, {});
    CHECK(get_or(b1->step(ctx, {}), "y") == get_or(b2->step(ctx, {}), "y"));
  }

  SECTION("math/deg_to_rad and math/rad_to_deg") {
    const auto* d2r = reg.find("math/deg_to_rad");
    const auto* r2d = reg.find("math/rad_to_deg");
    REQUIRE(d2r != nullptr);
    REQUIRE(r2d != nullptr);
    auto b_d2r = d2r->make({}, {});
    auto b_r2d = r2d->make({}, {});
    Context ctx;
    CHECK_THAT(get_or(b_d2r->step(ctx, {{"u", 180.0}}), "y"), WithinAbs(3.1415926535, 1e-6));
    CHECK_THAT(get_or(b_d2r->step(ctx, {{"u", 90.0}}), "y"), WithinAbs(1.5707963267, 1e-6));
    CHECK_THAT(get_or(b_r2d->step(ctx, {{"u", 3.1415926535}}), "y"), WithinAbs(180.0, 1e-4));
  }

  SECTION("math/wrap_angle and math/angle_diff") {
    const auto* wrap_t = reg.find("math/wrap_angle");
    const auto* diff_t = reg.find("math/angle_diff");
    REQUIRE(wrap_t != nullptr);
    REQUIRE(diff_t != nullptr);
    auto b_wrap = wrap_t->make({}, {});
    auto b_diff = diff_t->make({}, {});
    Context ctx;
    // 3*pi wraps to -pi or pi
    CHECK_THAT(std::abs(get_or(b_wrap->step(ctx, {{"u", 3.0 * 3.1415926535}}), "y")), WithinAbs(3.1415926535, 1e-4));
    // -2.5*pi wraps to -0.5*pi
    CHECK_THAT(get_or(b_wrap->step(ctx, {{"u", -2.5 * 3.1415926535}}), "y"), WithinAbs(-0.5 * 3.1415926535, 1e-4));
    // angle difference: 0.1 - 6.2 should be close to +0.183 (shortest path around circle)
    CHECK_THAT(get_or(b_diff->step(ctx, {{"a", 0.1}, {"b", 6.2}}), "y"), WithinAbs(0.1831853, 1e-4));
    // angle difference: opposite angles
    CHECK_THAT(get_or(b_diff->step(ctx, {{"a", 1.0}, {"b", 1.0}}), "y"), WithinAbs(0.0, 1e-6));
  }

  SECTION("math/atan2, math/hypot and math/deadband") {
    const auto* atan2_t = reg.find("math/atan2");
    const auto* hypot_t = reg.find("math/hypot");
    const auto* deadband_t = reg.find("math/deadband");
    REQUIRE(atan2_t != nullptr);
    REQUIRE(hypot_t != nullptr);
    REQUIRE(deadband_t != nullptr);
    auto b_atan2 = atan2_t->make({}, {});
    auto b_hypot = hypot_t->make({}, {});
    auto b_dead = deadband_t->make({{"width", 0.2}}, {});
    Context ctx;
    CHECK_THAT(get_or(b_atan2->step(ctx, {{"y", 1.0}, {"x", 1.0}}), "y"), WithinAbs(0.785398, 1e-4));
    CHECK_THAT(get_or(b_hypot->step(ctx, {{"a", 3.0}, {"b", 4.0}}), "y"), WithinAbs(5.0, 1e-6));
    CHECK(get_or(b_dead->step(ctx, {{"u", 0.1}}), "y") == 0.0);
    CHECK(get_or(b_dead->step(ctx, {{"u", -0.15}}), "y") == 0.0);
    CHECK_THAT(get_or(b_dead->step(ctx, {{"u", 0.5}}), "y"), WithinAbs(0.3, 1e-6));
    CHECK_THAT(get_or(b_dead->step(ctx, {{"u", -0.5}}), "y"), WithinAbs(-0.3, 1e-6));
  }
}

TEST_CASE("String Toolkit: All Blocks") {
  Registry reg = builtins();

  SECTION("string/concat") {
    const auto* t = reg.find("string/concat");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "++");

    auto b = t->make({{"inputs", 3.0}}, {{"separator", ", "}});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"a", std::string("apple")}, {"b", std::string("banana")}, {"c", 42.0}}), "y") == "apple, banana, 42");
  }

  SECTION("string/format") {
    const auto* t = reg.find("string/format");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "{…}");

    // Dynamic ports from template
    auto parsed = parse_placeholders("Hello {name}, your id is {id} and {{escaped}}!");
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0] == "name");
    CHECK(parsed[1] == "id");

    auto b = t->make({}, {{"template", "Point({x}, {y})"}});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"x", 10.5}, {"y", 20.0}}), "y") == "Point(10.5, 20)");
  }

  SECTION("string/split") {
    const auto* t = reg.find("string/split");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "a|b");

    auto b = t->make({{"index", 1.0}}, {{"delimiter", ","}});
    Context ctx;
    auto res = b->step(ctx, {{"text", std::string("10, 20.5, 30")}});
    CHECK(get_text(res, "y") == " 20.5");
    CHECK(get_or(res, "count") == 3.0);
    const auto* vec = get_vector(res, "vector");
    REQUIRE(vec != nullptr);
    REQUIRE(vec->size() == 3);
    CHECK((*vec)[0] == 10.0);
    CHECK((*vec)[1] == 20.5);
    CHECK((*vec)[2] == 30.0);
  }

  SECTION("string/replace") {
    const auto* t = reg.find("string/replace");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "a→b");

    auto b = t->make({{"all", 1.0}}, {{"find", "foo"}, {"replace", "bar"}});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"text", std::string("foo and foo")}}), "y") == "bar and bar");
  }

  SECTION("string/substring") {
    const auto* t = reg.find("string/substring");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);

    auto b = t->make({{"start", 2.0}, {"length", 3.0}}, {});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"text", std::string("hello world")}}), "y") == "llo");

    // Negative start
    auto b_neg = t->make({{"start", -5.0}, {"length", -1.0}}, {});
    CHECK(get_text(b_neg->step(ctx, {{"text", std::string("hello world")}}), "y") == "world");
  }

  SECTION("string/trim") {
    const auto* t = reg.find("string/trim");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);

    auto b = t->make({{"mode", 0.0}}, {});
    Context ctx;
    CHECK(get_text(b->step(ctx, {{"text", std::string("   padded string \t\n")}}), "y") == "padded string");
  }

  SECTION("string/length") {
    const auto* t = reg.find("string/length");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "#T");

    auto b = t->make({}, {});
    Context ctx;
    CHECK(get_or(b->step(ctx, {{"text", std::string("paperboat")}}), "y") == 9.0);
  }

  SECTION("string/change_case") {
    const auto* t = reg.find("string/change_case");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);

    Context ctx;
    auto upper_b = t->make({{"mode", 0.0}}, {});
    CHECK(get_text(upper_b->step(ctx, {{"text", std::string("hello world")}}), "y") == "HELLO WORLD");

    auto title_b = t->make({{"mode", 2.0}}, {});
    CHECK(get_text(title_b->step(ctx, {{"text", std::string("hello-world test")}}), "y") == "Hello-World Test");
  }

  SECTION("string/text_test") {
    const auto* t = reg.find("string/text_test");
    REQUIRE(t != nullptr);
    CHECK(t->def.view.compact == true);
    CHECK(t->def.view.glyph == "T?");

    Context ctx;
    auto contains_b = t->make({{"mode", 0.0}}, {});
    CHECK(get_or(contains_b->step(ctx, {{"text", std::string("foobar")}, {"test", std::string("oob")}}), "y") == 1.0);
    CHECK(get_or(contains_b->step(ctx, {{"text", std::string("foobar")}, {"test", std::string("xyz")}}), "y") == 0.0);

    auto step_b = t->make_step({{"mode", 1.0}}, {}); // starts_with
    CHECK(step_b->tick(ctx, {{"text", std::string("prefix_val")}, {"test", std::string("prefix")}}) == "yes");
    CHECK(step_b->tick(ctx, {{"text", std::string("prefix_val")}, {"test", std::string("val")}}) == "no");
  }
}
