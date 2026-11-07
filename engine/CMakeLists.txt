cmake_minimum_required(VERSION 3.25)
project(paperboat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Off when a backend pulls the engine in with add_subdirectory: it only needs the libraries.
option(PAPERBOAT_BUILD_TESTS "Build the Catch2 tests" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_BUILD_TOOLS "Build paperboat-catalog" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_BUILD_MODELICA "Build engine/modelica when omc is installed" ${PROJECT_IS_TOP_LEVEL})

# Dependencies: use an installed copy if there is one, otherwise fetch a pinned release.
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  FIND_PACKAGE_ARGS 3.11)
FetchContent_Declare(tomlplusplus
  URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
  FIND_PACKAGE_ARGS 3.4)
FetchContent_MakeAvailable(nlohmann_json tomlplusplus)

add_library(paperboat_warnings INTERFACE)
if(MSVC)
  target_compile_options(paperboat_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(paperboat_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# WebSocket for engine/core's server.
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.1.0.tar.gz
  SYSTEM)
FetchContent_MakeAvailable(ixwebsocket)

add_subdirectory(toolkit)
add_subdirectory(core)

# Optional toolkit packages are self-contained folders with a toolkit.toml and a
# CMakeLists.txt. The folder decides whether its dependency is available/enabled.
file(GLOB optional_toolkit_manifests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/*/toolkit.toml")
foreach(manifest IN LISTS optional_toolkit_manifests)
  get_filename_component(toolkit_dir "${manifest}" DIRECTORY)
  if(EXISTS "${toolkit_dir}/CMakeLists.txt")
    get_filename_component(toolkit_name "${toolkit_dir}" NAME)
    add_subdirectory("${toolkit_dir}" "toolkit/${toolkit_name}")
  endif()
endforeach()
# The omc bridge and Modelica generator (#160): optional, needs omc to run.
if(PAPERBOAT_BUILD_MODELICA)
  add_subdirectory(modelica)
endif()
if(PAPERBOAT_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
# Backends (examples/backends/*-cpp) are projects of their own that use the engine; the
# engine never builds or reads them.

if(PAPERBOAT_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
    FIND_PACKAGE_ARGS 3)
  FetchContent_MakeAvailable(Catch2)
  add_subdirectory(tests)
endif()
