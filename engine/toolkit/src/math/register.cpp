#include "common.hpp"

namespace paperboat::toolkit {

void register_math(Registry& registry) { math::add_arithmetic(registry); }

}  // namespace paperboat::toolkit
