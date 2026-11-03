#include "common.hpp"

namespace paperboat::toolkit {

void register_step(Registry& registry) { step::add_flow(registry); }

}  // namespace paperboat::toolkit
