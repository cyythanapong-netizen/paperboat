#include "common.hpp"

namespace paperboat::toolkit {

void register_control(Registry& registry) {
  control::add_pid(registry);
  control::add_simulator(registry);
}

}  // namespace paperboat::toolkit
