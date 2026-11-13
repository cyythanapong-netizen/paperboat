#pragma once

// Toolkits as plugins (pb#352): a toolkit built as a shared library registers its blocks into a
// host's registry at runtime, so installing one needs no engine rebuild. Needs the engine built with
// PAPERBOAT_SHARED_LIBS (one copy of the registry and the solvers' run defaults per process).
//
// A plugin is one translation unit with
//
//   #include <paperboat/toolkit/plugin.hpp>
//   PAPERBOAT_TOOLKIT_PLUGIN(paperboat::toolkit::opencv::register_opencv)
//
// built as libpaperboat_plugin_<name>.so (.dylib, .dll). The host refuses one built for another
// paperboat version or C++ standard library: the registry crosses the boundary as C++ objects.

#include <filesystem>
#include <string>
#include <vector>
#include <version>

#include "paperboat/toolkit/registry.hpp"

#if defined(_LIBCPP_VERSION)
#define PAPERBOAT_PLUGIN_STDLIB "libc++"
#elif defined(__GLIBCXX__)
#define PAPERBOAT_PLUGIN_STDLIB "libstdc++"
#elif defined(_MSC_VER)
#define PAPERBOAT_PLUGIN_STDLIB "msvc"
#else
#define PAPERBOAT_PLUGIN_STDLIB "unknown"
#endif

// What a plugin and its host must agree on. PAPERBOAT_SDK_VERSION comes from paperboat::toolkit.
#define PAPERBOAT_PLUGIN_ABI "paperboat " PAPERBOAT_SDK_VERSION ", " PAPERBOAT_PLUGIN_STDLIB

#if defined(_WIN32)
#define PAPERBOAT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PAPERBOAT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define PAPERBOAT_TOOLKIT_PLUGIN(register_fn)                                                        \
  PAPERBOAT_PLUGIN_EXPORT const char* paperboat_plugin_abi() { return PAPERBOAT_PLUGIN_ABI; }       \
  PAPERBOAT_PLUGIN_EXPORT void paperboat_plugin_register(::paperboat::toolkit::Registry& registry) { \
    register_fn(registry);                                                                           \
  }

namespace paperboat::toolkit {

struct PluginLoad {
  std::string name;  // "opencv", from libpaperboat_plugin_opencv.so
  std::filesystem::path path;
  bool ok = false;
  std::string message;  // why it was not loaded
};

// Where plugins are looked for, in order (the first plugin of a name wins): $PAPERBOAT_TOOLKIT_PATH
// (path-separated), $PAPERBOAT_HOME/toolkits, the running binary's build tree (<build>/toolkit) and
// an installed prefix's lib/paperboat/plugins.
std::vector<std::filesystem::path> plugin_dirs();

// Load every plugin under `dirs` (a few levels deep) into `registry`, skipping a package the registry
// already has. A plugin that fails to load, has another ABI or throws while registering is reported,
// never fatal. Plugins stay loaded for the life of the process.
std::vector<PluginLoad> load_plugins(Registry& registry, const std::vector<std::filesystem::path>& dirs = plugin_dirs());

}  // namespace paperboat::toolkit
