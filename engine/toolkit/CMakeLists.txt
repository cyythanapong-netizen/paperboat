add_library(paperboat_toolkit
  src/block.cpp
  src/bases.cpp
  src/expression.cpp
  src/registry.cpp
  src/descriptor.cpp
  # base: what every domain needs (values: number, text, boolean; key)
  src/base/values.cpp
  src/base/key.cpp
  src/base/logic.cpp
  src/base/register.cpp
  # signal: generators on the run clock (clock, sine, step, ramp, square)
  src/signal/waves.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/signal_blocks.cpp
  src/signal/register.cpp
  # math: arithmetic on signals (gain, sum, threshold, saturation)
  src/math/arithmetic.cpp
  src/math/register.cpp
  # dashboard: monitors (graph, display)
  src/dashboard/instruments.cpp
  src/dashboard/register.cpp
  # control: dynamics on a clock (PID, integrator, lag)
  src/control/pid.cpp
  src/control/simulator.cpp
  src/control/cart_pole.cpp
  src/control/register.cpp
  # step: a Step flow's path (start, wait, if, repeat)
  src/step/flow.cpp
  src/step/register.cpp
)
add_library(paperboat::toolkit ALIAS paperboat_toolkit)

# Packages' TOML definitions (blocks defined on C++ parents), embedded in the library.
file(READ src/signal/blocks.toml PAPERBOAT_SIGNAL_BLOCKS)
configure_file(src/embed.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/signal_blocks.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/signal/blocks.toml)

target_include_directories(paperboat_toolkit PUBLIC include PRIVATE src)
target_link_libraries(paperboat_toolkit
  PRIVATE nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus paperboat_warnings)
