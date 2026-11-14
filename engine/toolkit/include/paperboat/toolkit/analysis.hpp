#pragma once

// Analyses (pb#370): what a toolkit computes about a part of the graph on request, outside any
// run and any flow: a control loop's poles and margins, say. The app owns the entry point
// (Analyse on a subsystem or a selection) and the drawer; a toolkit registers the computation
// here (Registry::add) and the panel that shows its result in its web part
// (ToolkitWebHost.registerAnalysis). The catalog lists the analyses of the imported packages.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/toolkit/block.hpp"

namespace paperboat::toolkit {

using PortRef = std::pair<std::string, std::string>;  // (node id, port id)

// One signal block of the part being analysed, made with its params resolved (variables and
// formulas in place), as a run would make it.
struct AnalysisBlock {
  std::string id;
  BlockDef def;
  std::unique_ptr<Block> block;
  Values params;
  Config text;
};

// What an analysis gets: the blocks, the wires between them, and the boundary. Monitors and
// commented-out blocks are left out.
struct AnalysisRequest {
  std::vector<AnalysisBlock> blocks;  // dependencies first
  std::map<PortRef, PortRef> wires;   // an input → the output feeding it, both among `blocks`
  std::vector<PortRef> inputs;        // u: inputs fed from outside the part (or not wired)
  std::vector<PortRef> outputs;       // y: outputs read outside it
  nlohmann::json options = nlohmann::json::object();  // the analysis's own, from its panel
  double dt = 0.0;                    // the backend's run step, for an analysis that needs one

  const AnalysisBlock* find(std::string_view id) const {
    for (const auto& b : blocks)
      if (b.id == id) return &b;
    return nullptr;
  }
};

struct AnalysisDef {
  std::string label;
  std::string description = {};
};

struct AnalysisTemplate {
  std::string name;  // "<package>/<analysis>", e.g. "control/linear"
  AnalysisDef def;
  // The result, as the toolkit's panel reads it. Throws std::exception with what is wrong
  // (a block it can't handle, a part with no inputs); the message reaches the panel.
  std::function<nlohmann::json(const AnalysisRequest&)> run;
};

}  // namespace paperboat::toolkit
