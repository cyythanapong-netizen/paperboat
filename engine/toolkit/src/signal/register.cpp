#include "common.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace paperboat::toolkit {

extern const char* const kSignalBlocks;  // blocks.toml, embedded at build time

namespace signal {

void add_definitions(Registry& registry) {
  define_blocks(registry, "signal", kSignalBlocks);  // TOML on the C++ parent: sine, step, ramp, square, ease, f(t)
}

}  // namespace signal

}  // namespace paperboat::toolkit
