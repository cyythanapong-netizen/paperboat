#include "common.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace paperboat::toolkit {

extern const char* const kSignalBlocks;  // blocks.toml, embedded at build time

void register_signal(Registry& registry) {
  signal::add_waves(registry);                       // C++: clock, generator (the parent)
  define_blocks(registry, "signal", kSignalBlocks);  // TOML on it: sine, step, ramp, square, ease, f(t)
}

}  // namespace paperboat::toolkit
