#include "paperboat/core/graph.hpp"

#include <algorithm>
#include <set>

namespace paperboat::core {

namespace {

const toolkit::PortDef& port_def(const Node& node, const std::string& port, bool output) {
  const auto& ports = output ? node.def.outputs : node.def.inputs;
  auto it = std::ranges::find(ports, port, &toolkit::PortDef::id);
  if (it == ports.end())
    throw GraphError(node.id, node.id + " (" + node.def.type + ") has no " + (output ? "output" : "input") + " \"" + port + "\"");
  return *it;
}

}  // namespace

namespace {

// Sort `ids` so each comes after the ones it depends on. Throws on a cycle.
std::vector<std::string> sorted(const std::vector<std::string>& ids, std::map<std::string, std::vector<std::string>>& deps,
                                const std::string& where) {
  std::vector<std::string> order;
  std::set<std::string> done;
  while (order.size() < ids.size()) {
    std::vector<std::string> ready;
    for (const auto& id : ids) {
      if (done.contains(id)) continue;
      if (std::ranges::all_of(deps[id], [&](const std::string& d) { return done.contains(d); })) ready.push_back(id);
    }
    if (ready.empty()) {
      // What's left is in a loop, or waits for one: name a block that is in it.
      std::string in_loop;
      for (const auto& id : ids)
        if (!done.contains(id) && std::ranges::any_of(deps[id], [&](const std::string& d) { return !done.contains(d); })) {
          in_loop = id;
          break;
        }
      throw GraphError(in_loop, "graph has a cycle" + where + (in_loop.empty() ? "" : " through " + in_loop) +
                                    ": an algebraic loop. Break it with a block that doesn't pass its input straight through "
                                    "(a delay, an integrator)");
    }
    for (const auto& id : ready) {
      done.insert(id);
      order.push_back(id);
    }
  }
  return order;
}

toolkit::BlockDef story_def() {
  return {.type = "story",
          .label = "Story",
          .kind = toolkit::Kind::Function,
          .inputs = {{"in", "step"}, {"until", "signal"}},
          .outputs = {{"next", "step"}},
          .params = {{.id = "seconds", .default_value = 0}}};
}

}  // namespace

Graph prepare_graph(const json& graph, const toolkit::Catalog& catalog, Station& station) {
  Graph out;
  std::vector<std::string> ids;  // in graph order, so ties sort the same way every run

  // Story groups first: which blocks belong to which story.
  std::map<std::string, double> story_seconds;
  std::map<std::string, bool> story_reset;
  std::map<std::string, std::vector<PortRef>> story_release;
  for (const auto& g : graph.value("groups", json::array())) {
    if (g.value("kind", std::string{"visual"}) != "story") continue;
    const auto gid = g.at("id").get<std::string>();
    const json gp = g.value("params", json::object());
    if (!story_seconds.emplace(gid, number_param(gp, "seconds", 0.0)).second)
      throw GraphError(gid, "two groups are called " + gid);
    story_reset[gid] = number_param(gp, "reset", 0.0) != 0;
    for (const auto& r : g.value("release", json::array())) {
      if (!r.is_array() || r.size() != 2) throw GraphError(gid, "story " + gid + ": release lists outputs as [node, port]");
      story_release[gid].emplace_back(r[0].get<std::string>(), r[1].get<std::string>());
    }
    for (const auto& n : g.value("nodes", json::array())) {
      if (!out.story_of.emplace(n.get<std::string>(), gid).second)
        throw GraphError(n.get<std::string>(), "block " + n.get<std::string>() + " is in two story groups");
    }
  }
  for (const auto& n : graph.at("nodes")) {
    Node node{.id = n.at("id").get<std::string>(), .entry = nullptr, .def = {}, .block = nullptr, .step = nullptr,
              .object = nullptr, .monitor = nullptr};
    if (out.nodes.contains(node.id)) throw GraphError(node.id, "two blocks are called " + node.id);
    const auto type = n.at("type").get<std::string>();
    node.entry = catalog.find(type);
    if (!node.entry)
      throw GraphError(node.id, "block " + node.id + " has the unknown type \"" + type +
                                    "\": this backend doesn't offer it (anymore); replace or remove it");
    toolkit::Values params;
    toolkit::Config text;
    const json given = n.value("params", json::object());
    for (const auto& p : node.entry->def.params) {
      if (p.is_number()) params[p.id] = number_param(given, p.id, p.default_value);  // a boolean reads as 1 or 0
      else text[p.id] = given.value(p.id, p.default_text);
    }
    // A magic block's ports are those of its form.
    node.def = toolkit::in_form(node.entry->def, text.contains("form") ? text.at("form") : "");
    // A block whose param decides its inputs (Sum's signs) has one per character of it.
    const auto& from = node.entry->def.ports_from;
    node.def = toolkit::with_signs(node.def, from.empty() || !text.contains(from) ? "" : text.at(from));
    const auto& def = node.def;

    // Making a block can fail (a param it can't use, an expression that doesn't compile):
    // the error names the block.
    const auto made = [&](auto make) {
      try {
        return make();
      } catch (const GraphError&) {
        throw;
      } catch (const std::exception& err) {
        throw GraphError(node.id, node.id + ": " + err.what());
      }
    };
    if (node.entry->linked) {
      // A linked block is its station object's handle: same id, and the object must exist.
      auto it = std::ranges::find(station, node.id, &StationItem::id);
      if (it == station.end() || !it->entry->def.block || it->entry->def.block->type != type)
        throw GraphError(node.id, "block " + node.id + " is linked to a station object that isn't there; add it on the Station page");
      node.object = it->instance.get();
    } else if (def.kind == toolkit::Kind::Monitor) {
      // The core samples monitors; one from a template may turn its input into what it
      // shows (output "value", such as an image as a picture).
      if (node.entry->from && node.entry->from->make) node.monitor = made([&] { return node.entry->make(params, text); });
    } else if (toolkit::has_step_ports(def)) {
      if (!node.entry->from || !node.entry->from->make_step) throw GraphError(node.id, "no implementation for step block type: " + type);
      node.step = made([&] { return node.entry->make_step(params, text); });
      out.has_steps = true;
      const bool has_step_input = std::ranges::any_of(def.inputs, [](const auto& p) { return p.type == "step"; });
      if (!has_step_input) out.hats.push_back(node.id);
    } else {
      if (!node.entry->from || !node.entry->from->make) throw GraphError(node.id, "no implementation for block type: " + type);
      node.block = made([&] { return node.entry->make(params, text); });
    }
    node.params = std::move(params);
    node.text = std::move(text);
    ids.push_back(node.id);
    out.nodes.emplace(node.id, std::move(node));
  }

  // Each story becomes a step node of its own.
  const auto until_wired = [&](const std::string& gid) {
    return std::ranges::any_of(graph.at("edges"), [&](const json& e) { return e.at("to")[0] == gid && e.at("to")[1] == "until"; });
  };
  for (const auto& [gid, seconds] : story_seconds) {
    if (out.nodes.contains(gid)) throw GraphError(gid, "group " + gid + " has the id of a block");
    out.own.push_back({.def = story_def(), .from = nullptr, .config = {}, .group = {}, .linked = false});
    auto step = std::make_unique<StoryStep>(seconds, until_wired(gid));
    out.stories[gid].step = step.get();
    out.stories[gid].reset = story_reset[gid];
    for (const auto& [node, port] : story_release[gid]) {
      auto it = out.story_of.find(node);
      if (it == out.story_of.end() || it->second != gid)
        throw GraphError(gid, "story " + gid + " releases " + node + "." + port + ", but " + node + " isn't one of its blocks");
      const auto& outputs = out.nodes.at(node).def.outputs;
      if (std::ranges::find(outputs, port, &toolkit::PortDef::id) == outputs.end())
        throw GraphError(gid, "story " + gid + " releases " + node + "." + port + ", which " + node + " doesn't have");
    }
    out.stories[gid].release = story_release[gid];
    out.nodes.emplace(gid, Node{.id = gid, .entry = &out.own.back(), .def = story_def(), .block = nullptr,
                                .step = std::move(step), .object = nullptr, .monitor = nullptr});
    out.has_steps = true;
  }
  for (const auto& [block, gid] : out.story_of) {
    auto it = out.nodes.find(block);
    if (it == out.nodes.end()) throw GraphError(gid, "story " + gid + " lists " + block + ", which isn't a block of the graph");
    if (it->second.step) throw GraphError(block, "step block " + block + " can't be inside a story: a story runs signal blocks");
  }

  std::map<std::string, std::vector<std::string>> deps;
  for (const auto& e : graph.at("edges")) {
    PortRef from{e.at("from")[0], e.at("from")[1]}, to{e.at("to")[0], e.at("to")[1]};
    if (!out.nodes.contains(from.first) || !out.nodes.contains(to.first))
      throw GraphError("edge refers to an unknown node: " + from.first + " -> " + to.first);
    const auto& src = out.nodes.at(from.first);
    const auto& dst = out.nodes.at(to.first);
    // A wire joins ports of one type (step to step, image to image; "any" to anything but a
    // step), in one unit if both ends have one.
    const auto& out_port = port_def(src, from.second, true);
    if (auto why = toolkit::wire_mismatch(out_port, port_def(dst, to.second, false)))
      throw GraphError(to.first, "wire " + from.first + "." + from.second + " -> " + to.first + "." + to.second + " " + *why);
    const bool step = out_port.type == "step";
    if (step) {
      if (!out.nexts.emplace(from, to.first).second)
        throw GraphError(from.first, "step output " + from.first + "." + from.second + " has two wires; it can lead to one block");
      continue;
    }
    if (!out.wires.emplace(to, from).second)
      throw GraphError(to.first, "input " + to.first + "." + to.second + " has two wires; an input takes one");
    // A linked block's outputs come from its object's state, not its inputs (they push the
    // object after the pass), so a wire into one closes the loop through the station. Step
    // blocks read their signal inputs after the pass too.
    // Within one story, or outside every story, blocks wait for what feeds them. Across a
    // story's edge they don't: a story's results are read as they last were, and what
    // flows into a story was computed earlier in the clock step.
    const auto story = [&](const std::string& id) {
      auto it = out.story_of.find(id);
      return it == out.story_of.end() ? std::string() : it->second;
    };
    // A step's signal outputs are read as they were after the last clock step (like a
    // linked block's), so a wire from one never makes a cycle. A block without feedthrough
    // (an integrator, a delay) gives its outputs before it reads its inputs: a wire into
    // one doesn't make it wait, so a loop through it is allowed.
    const bool waits = !dst.object && !dst.step && dst.def.feedthrough;
    if (!src.step && waits && story(from.first) == story(to.first)) deps[to.first].push_back(from.first);
  }

  std::vector<std::string> outside;
  std::map<std::string, std::vector<std::string>> inside;
  for (const auto& id : ids) {
    if (out.nodes.at(id).step) continue;
    if (auto it = out.story_of.find(id); it != out.story_of.end()) inside[it->second].push_back(id);
    else outside.push_back(id);
  }
  out.order = sorted(outside, deps, "");
  for (auto& [gid, story] : out.stories) story.order = sorted(inside[gid], deps, " in story " + gid);
  return out;
}

}  // namespace paperboat::core
