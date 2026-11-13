// A plugin for plugin_test.cpp: one template, registered at load.
#include <paperboat/toolkit/plugin.hpp>

namespace {
void add(paperboat::toolkit::Registry& r) {
  paperboat::toolkit::Template t;
  t.name = "plugintest/one";
  t.def.type = "one";
  r.add(std::move(t));
}
}  // namespace

PAPERBOAT_TOOLKIT_PLUGIN(add)
