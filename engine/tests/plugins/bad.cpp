// A plugin built for another paperboat: plugin_test.cpp expects it refused.
#include <paperboat/toolkit/plugin.hpp>

PAPERBOAT_PLUGIN_EXPORT const char* paperboat_plugin_abi() { return "paperboat 0.0.0, libnope"; }
PAPERBOAT_PLUGIN_EXPORT void paperboat_plugin_register(paperboat::toolkit::Registry&) {}
