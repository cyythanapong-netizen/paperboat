// step/join.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"
#include "step/flow_shared.hpp"

namespace paperboat::toolkit::step {
namespace {

// Waits until `paths` paths have arrived, then goes on as one: the last to arrive leaves
// by next, the others end here (by joined, which nothing is wired to). Several paths may
// be in it at once. More arrivals start the next count.
class Join final : public Step {
 public:
  explicit Join(double paths) : paths_(std::max(1, static_cast<int>(std::lround(paths)))) {}
  void enter(const Context&, const Values&) override { ++arrived_; }
  std::string tick(const Context&, const Values&) override {
    if (arrived_ < paths_) return "";
    const bool first = released_++ == 0;
    if (released_ == paths_) {
      arrived_ -= paths_;
      released_ = 0;
    }
    return first ? "next" : "joined";
  }
  bool shared() const override { return true; }

 private:
  int paths_;
  int arrived_ = 0;
  int released_ = 0;
};

}  // namespace

void add_join(Registry& registry) {
  registry.add(make_step_template("join", "Join", {step_port("in")}, {step_port("next")}, {{"paths", 2.0}},
                                   [](const Values& p, const Config&) {
                                     return std::make_unique<Join>(get_or(p, "paths"));
                                   }, "Waits for paths to arrive, then goes on as one."));
}

}  // namespace paperboat::toolkit::step
