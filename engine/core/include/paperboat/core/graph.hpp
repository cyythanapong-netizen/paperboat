#pragma once

// A flow graph from the protocol's JSON, checked against a catalog and prepared to run:
// signal blocks sorted, step blocks wired into paths.

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "paperboat/core/station.hpp"

namespace paperboat::core {

using PortRef = std::pair<std::string, std::string>;  // (node id, port id)

struct Node {
  std::string id;
  const toolkit::CatalogEntry* entry = nullptr;
  toolkit::BlockDef def = {};                 // its entry's, in the form it is in (a magic block)
  std::unique_ptr<toolkit::Block> block;      // signal blocks (none for monitors)
  std::unique_ptr<toolkit::Step> step;        // step blocks
  toolkit::StationObject* object = nullptr;   // linked blocks: the station object they act on
  std::unique_ptr<toolkit::Block> monitor;    // monitors that convert what they show
  toolkit::Values params = {};                // what it was made with, to make it again
  toolkit::Config text = {};
  // An elementwise block fed a vector runs as lanes (toolkit::make_lanes): how many (0: the
  // length is known only at run time). Unset: it runs as itself, on numbers.
  std::optional<std::size_t> lanes = {};
  bool commented = false;
  std::string comment_mode = "out";  // "out" (omitted from execution) or "through" (bypassed)
  std::string comment = {};          // user notes or documentation
};

// A fresh instance of a signal block `node` (as it was made: lanes, if it runs as lanes).
std::unique_ptr<toolkit::Block> make_block(const Node& node);

// A story group's step: when a path arrives it runs the story's blocks (`pass`, set by
// the run) once per clock step for `seconds` (0: once), then leaves by "next". With its
// `until` input wired it leaves as soon as `until` is positive, and `seconds` is a time
// limit (0: none). Their outputs keep their last values, so what comes after reads the
// story's results, except those the group releases (`on_leave` drops them).
class StoryStep final : public toolkit::Step {
 public:
  StoryStep(double seconds, bool until) : seconds_(seconds), until_(until) {}
  std::function<void()> pass;
  std::function<void()> on_enter;  // set by the run for a story that starts afresh
  std::function<void()> on_leave;  // set by the run for a story that releases outputs
  void enter(const toolkit::Context&, const toolkit::Values&) override {
    elapsed_ = 0;
    if (on_enter) on_enter();
  }
  std::string tick(const toolkit::Context& ctx, const toolkit::Values& in) override {
    if (pass) pass();
    elapsed_ += ctx.dt;
    const bool timed_out = elapsed_ >= seconds_ - ctx.dt / 2;
    const bool done = until_ ? toolkit::get_or(in, "until") > 0 || (seconds_ > 0 && timed_out) : timed_out;
    if (done && on_leave) on_leave();
    return done ? "next" : "";
  }

 private:
  double seconds_;
  bool until_;  // its `until` input is wired
  double elapsed_ = 0;
};

// A story group: blocks that run as one step of a Step flow (see StoryStep).
struct Story {
  std::vector<std::string> order;  // its blocks, dependencies first
  StoryStep* step = nullptr;       // owned by its node
  bool reset = false;              // params.reset: its blocks start afresh each time a path arrives
  std::vector<PortRef> release;    // outputs that read as unwired once the path leaves
};

struct Graph {
  std::map<std::string, Node, std::less<>> nodes;
  std::vector<std::string> order;            // signal blocks outside stories, dependencies first
  std::map<PortRef, PortRef> wires;          // signal input → the output feeding it
  std::map<PortRef, std::string> nexts;      // step output → the block it leads to
  std::vector<std::string> hats;             // step blocks with no step input (Start, When)
  bool has_steps = false;                    // a Step flow
  std::map<std::string, Story, std::less<>> stories;  // by group id (also its node's id)
  std::map<std::string, std::string> story_of;        // block id → its story's id
  std::deque<toolkit::CatalogEntry> own;     // definitions the graph makes (a story's node)
};

// Check `graph` against the catalog and the station; make its blocks. `groups` (optional):
// a "visual" group only tidies the canvas and changes nothing here; a "story" group
// becomes one step node (same id as the group, step ports in → next and a signal input
// `until`, params `seconds` and `reset`) whose blocks run only while a path is at it;
// its `release` lists outputs ([node, port]) that drop when the path leaves.
// The graph's `flow`, if given, is its sheet's flow type: each block must work in it (a story's
// blocks in a Signal flow; toolkit::flow_mismatch).
// Wires must join ports of one type (step, signal, image…); a step output leads to one block;
// signal wires may not loop (linked blocks have no feedthrough, so wires into them don't
// count). Throws GraphError.
Graph prepare_graph(const json& graph, const toolkit::Catalog& catalog, Station& station);

}  // namespace paperboat::core
