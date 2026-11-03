#pragma once

// The station side of the toolkit: things on the Station page that the flow acts on.
//
// A station object owns its state and properties (mass, length, initial conditions),
// steps with the run clock, and draws itself. Its linked block on the Flow page is how a
// flow pushes on it (the block's inputs) and reads it (the block's outputs). ISA-88: the
// station is the physical model; its properties are set on the Station page only.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/block.hpp"

namespace paperboat::toolkit {

// One shape in the scene (the protocol's Entity).
struct Entity {
  std::string id = {};
  std::string shape = "box";                   // "box", "sphere" (size[0] = radius), "line" (pos → to)
                                          // or "image" (a size[0] × size[1] picture facing +z)
  std::array<double, 3> pos{};
  std::array<double, 3> size{1, 1, 1};
  std::array<double, 3> rot{};            // radians, x then y then z
  std::string color = "#888888";
  std::array<double, 3> scale{1, 1, 1};
  std::array<double, 3> to{};             // a line's end
  std::string src = {};                   // an image's picture: a URL or a data: URL
};

// One station object in a run.
class StationObject {
 public:
  virtual ~StationObject() = default;
  // The linked block's outputs, from the current state (no inputs needed: no feedthrough).
  virtual Values read() const = 0;
  // Advance one clock step under the linked block's inputs (unwired ones are absent).
  virtual void step(const Context& ctx, const Values& in) = 0;
  // Its shapes, ids prefixed with `id` + "/", drawn at depth `z` so objects don't overlap.
  virtual std::vector<Entity> entities(const std::string& id, double z) const = 0;
  // Another station object fills one of its link slots (ObjectDef::links): a spring's two
  // ends, a joint's parent. Called once, after every object of the station is made.
  virtual void link(std::string_view /*slot*/, StationObject& /*other*/) {}
};

// The state a domain's own blocks share during a run: a game, a document, an inbox, a
// physical system, whatever the backend is about. The core makes a fresh one per run,
// hands it to every block in Context::state, steps it after each pass of a clocked run,
// and draws it in the scene. Station objects are per-thing state the user sets up; this
// is the domain-wide state the backend owns. A domain doesn't need one.
class DomainState {
 public:
  virtual ~DomainState() = default;
  // Every entity it draws (for `scene`), and the ones that change (for `scene.patch`).
  virtual std::vector<Entity> entities() const { return {}; }
  virtual std::vector<Entity> patch() const { return entities(); }
  // Advance after a clocked pass, under what the blocks wrote. A message ends the run.
  virtual std::optional<std::string> step(const Context& /*ctx*/) { return std::nullopt; }
  // Logged when a run ends.
  virtual std::optional<std::string> summary() const { return std::nullopt; }
};

}  // namespace paperboat::toolkit
