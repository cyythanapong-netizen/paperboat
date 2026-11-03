# engine/core: the generic backend. Checks a station and a graph against a catalog, runs
# them, and serves the protocol over WebSocket (and a web station over HTTP, same port). A domain backend links it and
# provides only its descriptor, starters and own templates (GUIDELINES §6).

# The web station bridge (web/station.js), embedded so every backend serves it.
file(READ web/station.js PAPERBOAT_STATION_JS)
configure_file(src/station_js.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/station_js.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS web/station.js)

add_library(paperboat_core
  src/station.cpp
  src/graph.cpp
  src/run.cpp
  src/session.cpp
  src/serve.cpp
  src/main.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/station_js.cpp
)
add_library(paperboat::core ALIAS paperboat_core)

target_include_directories(paperboat_core PUBLIC include)
target_link_libraries(paperboat_core
  PUBLIC paperboat::toolkit nlohmann_json::nlohmann_json
  PRIVATE ixwebsocket paperboat_warnings)
