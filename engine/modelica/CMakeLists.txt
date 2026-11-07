# engine/modelica (paperboat::modelica): the omc bridge, the Modelica → backend generator and
# Squero Studio OM's squero/0 server (#160). Optional: skipped when omc isn't installed. No
# OpenModelica headers or libraries at build time; omc (and the libzmq it ships) at run time.
# engine/core and engine/toolkit never depend on it.

find_program(PAPERBOAT_OMC omc)
if(NOT PAPERBOAT_OMC)
  message(NOTICE "paperboat: omc (OpenModelica) not found; skipping engine/modelica")
  return()
endif()

add_library(paperboat_modelica src/omc.cpp src/generate.cpp src/squero.cpp)
add_library(paperboat::modelica ALIAS paperboat_modelica)
target_include_directories(paperboat_modelica PUBLIC include)
target_link_libraries(paperboat_modelica
  PRIVATE ixwebsocket nlohmann_json::nlohmann_json paperboat_warnings ${CMAKE_DL_LIBS})
