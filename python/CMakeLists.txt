# Paperboat's Python bindings: the `paperboat._engine` extension, built against the C++
# engine. Python adds domain code only; every framework feature is the engine's.
#
#   cmake -S python -B python/build && cmake --build python/build   (dev: lands in python/paperboat)
#   pip install ./python                                             (scikit-build-core)
#
# The release packages (pb#223) build it with -DPAPERBOAT_PYTHON_STABLE_ABI=ON and an
# output dir, and the engine's install ships it in <prefix>/lib/paperboat/python/paperboat
# (engine/CMakeLists.txt, PAPERBOAT_PYTHON_MODULES): no pip step for a user of the packages.
cmake_minimum_required(VERSION 3.25)
project(paperboat_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the engine's static libraries go into a shared module

# The CPython stable ABI (abi3): one module for every CPython from 3.12 on, as the packages and
# the wheel ship it (scikit-build-core asks for it with wheel.py-api, see pyproject.toml).
# With an older Python the module is for that version only.
option(PAPERBOAT_PYTHON_STABLE_ABI "Build paperboat._engine for the CPython stable ABI (Python >= 3.12)" OFF)
if(SKBUILD_SABI_COMPONENT)
  set(PAPERBOAT_PYTHON_STABLE_ABI ON)
endif()
set(PAPERBOAT_PYTHON_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/paperboat" CACHE PATH
    "Where a build that is not pip's puts the module (default: in place, next to paperboat/__init__.py)")

set(_pb_sabi)
if(PAPERBOAT_PYTHON_STABLE_ABI)
  set(_pb_sabi Development.SABIModule)
endif()
find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS ${_pb_sabi})

include(FetchContent)
# nanobind is used through its CMake config, not add_subdirectory: SOURCE_SUBDIR points at
# a folder without a CMakeLists.txt, so MakeAvailable only downloads it.
FetchContent_Declare(nanobind
  GIT_REPOSITORY https://github.com/wjakob/nanobind
  GIT_TAG v2.9.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(nanobind)
find_package(nanobind CONFIG REQUIRED PATHS "${nanobind_SOURCE_DIR}/cmake" NO_DEFAULT_PATH)

set(PAPERBOAT_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/../engine" CACHE PATH "Paperboat engine sources")
add_subdirectory(${PAPERBOAT_ENGINE_DIR} paperboat EXCLUDE_FROM_ALL)

set(_pb_abi)
if(PAPERBOAT_PYTHON_STABLE_ABI)
  if(Python_VERSION VERSION_LESS 3.12)
    message(STATUS "paperboat._engine: Python ${Python_VERSION} has no stable ABI for nanobind; the module is for ${Python_VERSION} only")
  elseif(NOT TARGET Python::SABIModule)
    message(FATAL_ERROR "PAPERBOAT_PYTHON_STABLE_ABI: CMake found no stable-ABI Python library (Development.SABIModule)")
  else()
    set(_pb_abi STABLE_ABI)
  endif()
endif()
nanobind_add_module(_engine ${_pb_abi} src/engine.cpp)
target_link_libraries(_engine PRIVATE paperboat::core)

if(SKBUILD)
  install(TARGETS _engine LIBRARY DESTINATION paperboat)
else()
  # $<1:...>: multi-config generators (Visual Studio) would otherwise add a Release/ or Debug/ folder
  set_target_properties(_engine PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<1:${PAPERBOAT_PYTHON_OUTPUT_DIR}>")
endif()
