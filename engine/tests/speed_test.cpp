// A run's speed changed while it runs (run.speed, #219): pacing goes on from where the run is,
// at the new speed, without jumping ahead or stalling; the session checks what the UI sends.

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
using core::json;
using Catch::Matchers::ContainsSubstring;

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

struct Paced {
  std::vector<json> messages;
  double wall = 0;  // s the run took
  std::string status;

  std::vector<json> of(const std::string& type) const {
    std::vector<json> out;
    for (const auto& m : messages)
      if (m["type"] == type) out.push_back(m);
    return out;
  }
  double last_fraction() const {
    for (auto m = messages.rbegin(); m != messages.rend(); ++m)
      if ((*m)["type"] == "run.progress") return (*m)["fraction"];
    return 0;
  }
  bool lagged() const {
    for (const auto& m : of("log"))
      if (m["text"].get<std::string>().find("computation delay") != std::string::npos) return true;
    return false;
  }
  // The run clock when the run took its n-th run.speed: the run.progress just before its answer.
  double t_at_speed(std::size_t n, double duration) const {
    double fraction = 0;
    std::size_t seen = 0;
    for (const auto& m : messages) {
      if (m["type"] == "run.progress") fraction = m["fraction"];
      if (m["type"] == "run.speed" && seen++ == n) return fraction * duration;
    }
    return -1;
  }
};

// The rig's starter, paced in real time, asking for a new speed whenever `ask` (given the
// wall-clock seconds so far and what the run said) has one.
Paced paced_run(core::RunSettings settings, std::function<std::optional<double>(double, const Paced&)> ask) {
  settings.real_time = true;
  const core::Backend backend{.engine = "test", .catalog = &rig::catalog(), .run = settings};
  Paced out;
  const auto start = Clock::now();
  const core::SpeedNow speed = [&] { return ask(since(start), out); };
  out.status = core::run("r", json::parse(rig::catalog().graph), json::parse(rig::catalog().station), backend,
                         [&](const json& m) { out.messages.push_back(m); }, nullptr, {}, nullptr, nullptr, nullptr, speed);
  out.wall = since(start);
  return out;
}

// Asks for each speed of `plan` once the wall clock passes its time.
std::function<std::optional<double>(double, const Paced&)> at_wall(std::vector<std::pair<double, double>> plan) {
  return [plan = std::move(plan), next = std::size_t{0}](double wall, const Paced&) mutable -> std::optional<double> {
    if (next < plan.size() && wall >= plan[next].first) return plan[next++].second;
    return std::nullopt;
  };
}

}  // namespace

TEST_CASE("run.speed: a run sped up mid-run goes on from where it is, faster, without a false delay") {
  // 2 s at 1×, then 4× after 0.5 s: 0.5 + 1.5 / 4 ≈ 0.9 s. (The old anchor, t / speed from the
  // start, would have found itself 0.4 s behind: a delay warning, then a race to 0.5 s.)
  const auto run = paced_run({.dt = 0.01, .duration = 2, .speed = 1}, at_wall({{0.5, 4}}));
  CHECK(run.status == "done");
  CHECK(run.wall > 0.75);
  CHECK(run.wall < 1.5);
  CHECK_FALSE(run.lagged());
  REQUIRE(run.of("run.speed").size() == 1);
  CHECK(run.of("run.speed")[0] == json{{"type", "run.speed"}, {"run", "r"}, {"speed", 4.0}});
}

TEST_CASE("run.speed: a run slowed down mid-run goes on from where it is, slower, without stalling") {
  // 2 s at 4×, then 1× after 0.25 s (t ≈ 1): 0.25 + 1 ≈ 1.25 s. (The old anchor would wait until
  // t / 1 from the start: 2 s.)
  const auto run = paced_run({.dt = 0.01, .duration = 2, .speed = 4}, at_wall({{0.25, 1}}));
  CHECK(run.status == "done");
  CHECK(run.wall > 1.05);
  CHECK(run.wall < 1.75);
  CHECK_FALSE(run.lagged());
  REQUIRE(run.of("run.speed").size() == 1);
  CHECK(run.of("run.speed")[0]["speed"] == 1.0);
}

TEST_CASE("run.speed: max finishes at once; back from max the pace starts where the run got to") {
  SECTION("1× to max: a 10 s run ends early") {
    const auto run = paced_run({.dt = 0.01, .duration = 10, .speed = 1}, at_wall({{0.2, 0}}));
    CHECK(run.status == "done");
    CHECK(run.wall < 3);
    REQUIRE(run.of("run.speed").size() == 1);
    CHECK(run.of("run.speed")[0]["speed"] == 0.0);
  }
  SECTION("max to 1× and back") {
    // At max until t = 20, then 1× for 0.3 s of wall clock, then max again. The old anchor would
    // have slept until t / 1 from the start (20 s) before the run moved again.
    std::optional<double> slowed_at;
    bool done_asking = false;
    const auto run = paced_run({.dt = 0.01, .duration = 100, .speed = 0}, [&](double wall, const Paced& sofar) -> std::optional<double> {
      if (done_asking) return std::nullopt;
      if (!slowed_at) {
        if (sofar.last_fraction() < 0.2) return std::nullopt;
        slowed_at = wall;
        return 1.0;
      }
      if (wall - *slowed_at < 0.3) return std::nullopt;
      done_asking = true;
      return 0.0;
    });
    CHECK(run.status == "done");
    REQUIRE(run.of("run.speed").size() == 2);
    const double paced = run.t_at_speed(1, 100) - run.t_at_speed(0, 100);
    CHECK(paced > 0.15);  // 0.3 s at 1×, give or take a frame and the test machine
    CHECK(paced < 0.7);
    CHECK(run.wall < 8);
    CHECK_FALSE(run.lagged());
  }
}

TEST_CASE("run.speed: the session refuses what isn't a speed, ignores one between runs, hands the rest to the run") {
  static const core::Backend backend{.engine = "test",
                                     .catalog = &rig::catalog(),
                                     .run = {.dt = 0.01, .duration = 10, .real_time = true, .speed = 1}};
  std::vector<json> sent;
  std::mutex mutex;
  auto count = [&] {
    std::lock_guard lock(mutex);
    return sent.size();
  };
  auto last = [&] {
    std::lock_guard lock(mutex);
    return sent.back();
  };
  auto at = [&](std::size_t index) {
    std::lock_guard lock(mutex);
    return sent.at(index);
  };
  core::Session session(backend, [&](const std::string& text) {
    std::lock_guard lock(mutex);
    sent.push_back(json::parse(text));
  });

  // Between runs: nothing to change, and nothing said (not an unknown message either).
  auto before = count();
  session.on_message(R"({"type": "run.speed", "speed": 2})");
  CHECK(count() == before);

  for (const char* bad : {R"({"type": "run.speed", "speed": -1})", R"({"type": "run.speed", "speed": "fast"})",
                          R"({"type": "run.speed"})"}) {
    before = count();
    session.on_message(bad);
    REQUIRE(count() == before + 1);
    CHECK(last()["type"] == "log");
    CHECK(last()["level"] == "error");
    CHECK_THAT(last()["text"].get<std::string>(), ContainsSubstring("run.speed: speed must be"));
  }

  const auto start = Clock::now();
  session.on_message(R"({"type": "graph.run", "graph": )" + rig::catalog().graph + "}");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const auto refused = count();
  session.on_message(R"({"type": "run.speed", "speed": -3})");  // refused: the run goes on at 1×
  CHECK(at(refused)["level"] == "error");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  session.on_message(R"({"type": "run.speed", "speed": 0})");  // max: the 10 s run ends early
  for (int i = 0; i < 500 && last()["type"] != "run.finished"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE(last()["type"] == "run.finished");
  CHECK(last()["status"] == "done");
  CHECK(since(start) < 4);

  std::vector<json> answers;
  {
    std::lock_guard lock(mutex);
    for (const auto& m : sent)
      if (m["type"] == "run.speed") answers.push_back(m);
  }
  REQUIRE(answers.size() == 1);  // the run took 0, not -3
  CHECK(answers[0] == json{{"type", "run.speed"}, {"run", "run-1"}, {"speed", 0.0}});

  // After it: ignored again.
  before = count();
  session.on_message(R"({"type": "run.speed", "speed": 1})");
  CHECK(count() == before);
}
