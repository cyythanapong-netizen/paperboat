# Paperboat's Python bindings: the `paperboat._engine` extension, built against the C++
# engine. Python adds domain code only; every framework feature is the engine's.
#
#   cmake -S python -B python/build && cmake --build python/build   (dev: lands in python/paperboat)
#   pip install ./python                                             (scikit-build-core)
cmake_minimum_required(VERSION 3.25)
project(paperboat_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the engine's static libraries go into a shared module

find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)

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

nanobind_add_module(_engine src/engine.cpp)
target_link_libraries(_engine PRIVATE paperboat::core)

if(SKBUILD)
  install(TARGETS _engine LIBRARY DESTINATION paperboat)
else()
  set_target_properties(_engine PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/paperboat")
endif()
