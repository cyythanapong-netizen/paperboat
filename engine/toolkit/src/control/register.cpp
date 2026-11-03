#include "common.hpp"

namespace paperboat::toolkit {

void register_control(Registry& registry) {
  control::add_pid(registry);
  control::add_simulator(registry);
  control::add_cart_pole(registry);
}

}  // namespace paperboat::toolkit
