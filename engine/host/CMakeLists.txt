# libpaperboat_host: the C ABI that lets a non-C++ runtime (Julia) serve a backend whose
# domain code lives there. The engine, its registry, catalog, runs and protocol are the
# same code the C++ and Python profiles use; the host answers events.

add_library(paperboat_host SHARED src/julia_host.cpp)
add_library(paperboat::host ALIAS paperboat_host)
target_include_directories(paperboat_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(paperboat_host PRIVATE paperboat::core paperboat::toolkit paperboat_warnings)
set_target_properties(paperboat_host PROPERTIES
  OUTPUT_NAME paperboat_host
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(PAPERBOAT_INSTALL)
  install(TARGETS paperboat_host LIBRARY DESTINATION lib RUNTIME DESTINATION bin COMPONENT runtime)
endif()
