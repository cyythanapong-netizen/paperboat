# The smallest backend that builds against the engine as a project of its own: CI builds it against the
# installed SDK (find_package(Paperboat)) and runs `paperboat check` on it. Not part of the engine build.
cmake_minimum_required(VERSION 3.25)
project(paperboat_sdk_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The engine: the sources of a Paperboat checkout (-DPAPERBOAT_ENGINE_DIR=…), else an installed SDK.
if(PAPERBOAT_ENGINE_DIR)
  add_subdirectory(${PAPERBOAT_ENGINE_DIR} paperboat EXCLUDE_FROM_ALL)
else()
  find_package(Paperboat 0.1 CONFIG REQUIRED)
endif()
# The program lands in build/ under every generator (backend.toml's run), also multi-config ones.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")

add_executable(paperboat-sdk-backend main.cpp)
target_link_libraries(paperboat-sdk-backend PRIVATE paperboat::core paperboat::warnings)
target_compile_definitions(paperboat-sdk-backend PRIVATE
  PAPERBOAT_DESCRIPTOR="${CMAKE_CURRENT_SOURCE_DIR}/descriptor.toml")
