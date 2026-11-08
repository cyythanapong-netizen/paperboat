cmake_minimum_required(VERSION 3.25)
project(paperboat VERSION 0.1.0 LANGUAGES CXX)  # the one version: --version, packages, and the v<version> release tag

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Off when a backend pulls the engine in with add_subdirectory: it only needs the libraries.
option(PAPERBOAT_BUILD_TESTS "Build the Catch2 tests" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_BUILD_TOOLS "Build paperboat-catalog" ${PROJECT_IS_TOP_LEVEL})
option(PAPERBOAT_STATIC_RUNTIME "Link libstdc++ and libgcc statically, so a release binary needs only glibc" OFF)
# The SDK (pb#177): install paperboat::core, paperboat::toolkit and their headers, so a backend can
# find_package(Paperboat) instead of building the engine from source.
option(PAPERBOAT_INSTALL "Install the SDK (libraries, headers, CMake config)" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
if(PAPERBOAT_INSTALL)
  # The libraries and their dependencies go to the "dev" component (paperboat-dev); bin/ and share/ name "runtime".
  set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME dev)
  set(JSON_Install ON CACHE BOOL "" FORCE)
endif()

if(MSVC AND PAPERBOAT_STATIC_RUNTIME)  # no vcruntime DLL to install next to the .exe; set before the dependencies are fetched
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
add_library(paperboat::warnings ALIAS paperboat_warnings)
set_target_properties(paperboat_warnings PROPERTIES EXPORT_NAME warnings)
if(MSVC)
  target_compile_options(paperboat_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(paperboat_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# WebSocket for engine/core's server.
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL ${PAPERBOAT_INSTALL} CACHE BOOL "" FORCE)  # the SDK links it: core uses it privately
FetchContent_Declare(ixwebsocket SYSTEM  # SYSTEM goes right after the name: CMake 3.25 reads it at the end as a URL
  URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.1.0.tar.gz)
FetchContent_MakeAvailable(ixwebsocket)

add_subdirectory(toolkit)
add_subdirectory(core)

if(PAPERBOAT_INSTALL)
  include(CMakePackageConfigHelpers)
  set(PAPERBOAT_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Paperboat)
  install(TARGETS paperboat_core paperboat_toolkit paperboat_warnings EXPORT PaperboatTargets COMPONENT dev
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY core/include/ toolkit/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev)
  install(EXPORT PaperboatTargets NAMESPACE paperboat:: DESTINATION ${PAPERBOAT_CMAKE_DIR} COMPONENT dev)
  configure_package_config_file(cmake/PaperboatConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/PaperboatConfig.cmake
                                INSTALL_DESTINATION ${PAPERBOAT_CMAKE_DIR})
  # 0.x: a backend built against 0.1 needs a 0.1 engine (SameMinorVersion).
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/PaperboatConfigVersion.cmake COMPATIBILITY SameMinorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PaperboatConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/PaperboatConfigVersion.cmake
          DESTINATION ${PAPERBOAT_CMAKE_DIR} COMPONENT dev)
endif()

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
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/paperboatlab/paperboat")
  set(CPACK_PACKAGE_FILE_NAME "paperboat-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_GENERATOR TGZ)
  if(WIN32)
    set(CPACK_GENERATOR ZIP)
  endif()
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION science)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "git, nodejs, npm")
  # Two .debs: paperboat (bin, share) and paperboat-dev (the SDK). The .tar.gz and .zip hold both.
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_ALL runtime dev)
  set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME paperboat)
  set(CPACK_DEBIAN_DEV_PACKAGE_NAME paperboat-dev)
  set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "paperboat (= ${PROJECT_VERSION}), cmake (>= 3.25), g++ (>= 4:12)")
  set(CPACK_DEBIAN_DEV_PACKAGE_SHLIBDEPS OFF)  # static libraries
  set(CPACK_DEBIAN_DEV_PACKAGE_SECTION libdevel)
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
