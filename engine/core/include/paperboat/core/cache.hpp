#pragma once

// What a run that evaluates once keeps between runs: each block's outputs, so the next run
// reuses those of blocks nothing changed for and computes only the rest (a reused block's
// node.state is "done" with no "running" before it). A session keeps one; a run is given it
// (core::run's `cache`).
//
// An entry is the block's last good result, by node id, with all it was made from: what the
// block is (type, ports, params, text, lanes, the run's dt and solver, its "file" params'
// files), the values that came in on its wires, and the variables it read. A later run reuses
// it only when all of these are the same now. Comparing the values on the wires (not the
// graph upstream) is what lets a change stop where it stops mattering: a block upstream that
// recomputes to the same values leaves everything after it reused, and a station object
// changed leaves the blocks fed by the others reused.
//
// Only blocks that may be reused are kept (BlockDef::cacheable, else Block::stateless());
// a block that set a variable, or was still being computed when the run was cancelled, is
// not. Values compare exactly: numbers bit for bit, text and lists by content, a handle by
// the object it holds (an entry keeps that alive, so the comparison can't be fooled).

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <paperboat/toolkit/block.hpp>
#include <paperboat/toolkit/station.hpp>

namespace paperboat::core {

// Whether two values are the same for the cache (see above).
bool same_value(const toolkit::Value& a, const toolkit::Value& b);
bool same_values(const toolkit::Values& a, const toolkit::Values& b);

class RunCache {
 public:
  struct Entry {
    std::string signature;  // what the block is (Run makes it): compact JSON
    toolkit::Values inputs;  // what came in on its wires (unwired inputs absent)
    // The variables it read through Context::variable, and what they were (none: no such variable).
    std::map<std::string, std::optional<toolkit::Value>, std::less<>> variables;
    toolkit::Values outputs;
    std::vector<toolkit::Entity> entities;                 // what it drew (Block::entities)
    std::vector<std::pair<std::string, std::string>> logs;  // (level, text) it wrote, shown again
  };

  // The entry for `node` if it was made from `signature` and `inputs`, and each variable it
  // read has the same value in `variables` now; else null.
  std::shared_ptr<const Entry> find(std::string_view node, std::string_view signature, const toolkit::Values& inputs,
                                    const toolkit::Values& variables) const;
  void store(const std::string& node, Entry entry);
  void erase(std::string_view node);
  // Drops the entries of nodes not in `nodes` (blocks the graph no longer has).
  void keep_only(const std::set<std::string, std::less<>>& nodes);
  void clear();
  std::size_t size() const;
  bool contains(std::string_view node) const;

 private:
  mutable std::mutex mutex_;  // a cancelled run may still be ending while the next one starts
  std::map<std::string, std::shared_ptr<const Entry>, std::less<>> entries_;
};

}  // namespace paperboat::core
