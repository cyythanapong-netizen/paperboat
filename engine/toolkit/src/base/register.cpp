#include "common.hpp"

namespace paperboat::toolkit {

void register_base(Registry& registry) {
  base::add_values(registry);
  base::add_key(registry);
}

}  // namespace paperboat::toolkit
