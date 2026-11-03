#pragma once

// A flow graph from the protocol's JSON, checked against a catalog and prepared to run:
// signal blocks sorted, step blocks wired into paths.

#include <deque>
#include <functional>
#include <map>
#include <memory>
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
};

// A story group's step: when a path arrives it runs the story's blocks (`pass`, set by
// the run) once per clock step for `seconds` (0: once), then leaves by "next". Their
// outputs keep their last values, so what comes after reads the story's results.
class StoryStep final : public toolkit::Step {
 public:
  explicit StoryStep(double seconds) : seconds_(seconds) {}
  std::function<void()> pass;
  std::function<void()> on_enter;  // set by the run for a story that starts afresh
  void enter(const toolkit::Context&, const toolkit::Values&) override {
    elapsed_ = 0;
    if (on_enter) on_enter();
  }
  std::string tick(const toolkit::Context& ctx, const toolkit::Values&) override {
    if (pass) pass();
    elapsed_ += ctx.dt;
    return elapsed_ >= seconds_ - ctx.dt / 2 ? "next" : "";
  }

 private:
  double seconds_;
  double elapsed_ = 0;
};

// A story group: blocks that run as one step of a Step flow (see StoryStep).
struct Story {
  std::vector<std::string> order;  // its blocks, dependencies first
  StoryStep* step = nullptr;       // owned by its node
  bool reset = false;              // params.reset: its blocks start afresh each time a path arrives
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
// becomes one step node (same id as the group, step ports in → next, params `seconds` and
// `reset`) whose blocks run only while a path is at it.
// Wires must join ports of one type (step, signal, image…); a step output leads to one block;
// signal wires may not loop (linked blocks have no feedthrough, so wires into them don't
// count). Throws GraphError.
Graph prepare_graph(const json& graph, const toolkit::Catalog& catalog, Station& station);

}  // namespace paperboat::core
