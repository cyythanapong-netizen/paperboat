#include "common.hpp"

namespace paperboat::toolkit {

void register_dashboard(Registry& registry) { dashboard::add_instruments(registry); }

}  // namespace paperboat::toolkit
