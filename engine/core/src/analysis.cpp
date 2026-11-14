// analysis.request (pb#370): a toolkit's analysis of a part of the graph, outside any run.
// The part's blocks are made as a run makes them (variables resolved, graph checked), then
// handed to the analysis the catalog lists under that name.

#include "paperboat/core/graph.hpp"
#include "paperboat/core/run.hpp"
#include "paperboat/core/station.hpp"

#include <paperboat/toolkit/analysis.hpp>
#include <paperboat/toolkit/descriptor.hpp>
#include <paperboat/toolkit/variables.hpp>

namespace paperboat::core {

namespace {

std::vector<toolkit::PortRef> port_list(const json& list, const char* what) {
  std::vector<toolkit::PortRef> out;
  if (list.is_null()) return out;
  if (!list.is_array()) throw std::invalid_argument(std::string(what) + ": expected a list of [node, port]");
  for (const auto& p : list) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_string() || !p[1].is_string())
      throw std::invalid_argument(std::string(what) + ": expected [node, port], got " + p.dump());
    out.emplace_back(p[0].get<std::string>(), p[1].get<std::string>());
  }
  return out;
}

}  // namespace

json analyse(const json& msg, const Backend& backend, const json& station, const json& variables) {
  const std::string id = msg.value("id", "");
  const std::string name = msg.value("analysis", "");
  json out = {{"type", "analysis.result"}, {"id", id}, {"analysis", name}};
  try {
    const auto& catalog = *backend.catalog;
    const auto it = std::ranges::find_if(catalog.analyses, [&](const toolkit::AnalysisTemplate* a) { return a->name == name; });
    if (it == catalog.analyses.end()) throw std::invalid_argument("this backend has no analysis \"" + name + "\"");

    const toolkit::Variables vars(msg.contains("variables") ? msg.at("variables") : variables);
    const json graph_json = toolkit::resolve_graph(msg.value("graph", json::object()), catalog, vars);
    Station objects = make_station(toolkit::resolve_station(station, catalog, vars), catalog);
    Graph g = prepare_graph(graph_json, catalog, objects, /*loops=*/true);  // the analysis solves them

    toolkit::AnalysisRequest request;
    request.dt = backend.run.dt;
    request.options = msg.value("options", json::object());
    for (const auto& node_id : g.order) {
      Node& node = g.nodes.at(node_id);
      if (!node.block || node.commented) continue;  // monitors, commented-out blocks
      request.blocks.push_back({.id = node.id, .def = node.def, .block = std::move(node.block), .params = node.params, .text = node.text});
    }
    for (const auto& [to, from] : g.wires)
      if (request.find(to.first) && request.find(from.first)) request.wires.emplace(to, from);
    request.inputs = port_list(msg.value("inputs", json()), "inputs");
    request.outputs = port_list(msg.value("outputs", json()), "outputs");
    out["result"] = (*it)->run(request);
  } catch (const std::exception& err) {
    out["error"] = err.what();
  }
  return out;
}

}  // namespace paperboat::core
