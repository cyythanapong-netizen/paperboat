#include "paperboat/toolkit/block.hpp"
#include "paperboat/toolkit/solve.hpp"

#include <utility>
#include <vector>

namespace paperboat::toolkit::solve {

ParamDef param(std::string fallback) {
  std::vector<std::string> options = {"auto"};
  options.insert(options.end(), names().begin(), names().end());
  return {.id = "solver", .type = "enum", .default_text = std::move(fallback), .options = std::move(options)};
}

}  // namespace paperboat::toolkit::solve
