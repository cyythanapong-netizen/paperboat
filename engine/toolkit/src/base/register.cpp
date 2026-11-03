#include "common.hpp"

namespace paperboat::toolkit {

void register_base(Registry& registry) {
  base::add_values(registry);
  base::add_key(registry);
  base::add_logic(registry);
}

}  // namespace paperboat::toolkit
