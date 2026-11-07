cmake_minimum_required(VERSION 3.25)
project(paperboat VERSION 0.1.0 LANGUAGES CXX)  # the one version: --version, packages, and the v<version> release tag

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Off when a backend pulls the engine in with add_subdirectory: it only needs the libraries.
option(PAPERBOAT_BUILD_TESTS "Build the Catch2 tests" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_BUILD_TOOLS "Build paperboat-catalog" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_BUILD_MODELICA "Build engine/modelica when omc is installed" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_MODELICA_WITHOUT_OMC "Build engine/modelica even when omc is not installed (release packages: omc is only needed at run time)" OFF)
option(PAPERBOAT_STATIC_RUNTIME "Link libstdc++ and libgcc statically, so a release binary needs only glibc" OFF)

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
FetchContent_Declare(ixwebsocket SYSTEM  # SYSTEM goes right after the name: CMake 3.25 reads it at the end as a URL
  URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.1.0.tar.gz)
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

# Packages (cpack -G DEB / TGZ / ZIP): the paperboat binary and the registries it reads (see tools/).
if(PROJECT_IS_TOP_LEVEL)
  set(CPACK_PACKAGE_NAME paperboat)
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Paperboat: a framework for engineering apps (scene, node graph, live results)")
  set(CPACK_PACKAGE_CONTACT "paperboat <noreply@github.com>")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/cyythanapong-netizen/paperboat")
  set(CPACK_PACKAGE_FILE_NAME "paperboat-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_GENERATOR TGZ)
  if(WIN32)
    set(CPACK_GENERATOR ZIP)
  endif()
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION science)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "git, nodejs, npm, openmodelica")
  include(CPack)
endif()

if(PAPERBOAT_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
    FIND_PACKAGE_ARGS 3)
  FetchContent_MakeAvailable(Catch2)
  add_subdirectory(tests)
endif()
