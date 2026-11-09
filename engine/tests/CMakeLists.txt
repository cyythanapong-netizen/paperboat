# Windows builds only the process-layer and solver tests; the rest of the suite uses POSIX (setenv, sh, /tmp).
if(WIN32)
  add_executable(paperboat_process_tests process_test.cpp)
  target_link_libraries(paperboat_process_tests PRIVATE paperboat::core Catch2::Catch2WithMain paperboat_warnings)
  add_executable(paperboat_solve_tests solve_test.cpp)  # MSVC with SUNDIALS' cvode and ida too
  target_link_libraries(paperboat_solve_tests PRIVATE paperboat::toolkit Catch2::Catch2WithMain paperboat_warnings)
  include(Catch)
  catch_discover_tests(paperboat_process_tests)
  catch_discover_tests(paperboat_solve_tests)
  return()
endif()

add_executable(paperboat_tests
  descriptor_test.cpp
  control_test.cpp
  solve_test.cpp
  interactive_test.cpp
  step_test.cpp
  station_test.cpp
  core_test.cpp
  cache_test.cpp
  speed_test.cpp
  variables_test.cpp
  connection_test.cpp
  toolkit_phase1_test.cpp
  math_signal_test.cpp
  vector_test.cpp
  list_test.cpp
  flow_type_test.cpp
  cli_test.cpp
  apps_test.cpp
  process_test.cpp
)
target_link_libraries(paperboat_tests
  PRIVATE paperboat::toolkit paperboat::core nlohmann_json::nlohmann_json Catch2::Catch2WithMain paperboat_warnings)
if(TARGET paperboat_toolkit_optimization)
  target_sources(paperboat_tests PRIVATE optimization_test.cpp)
  target_link_libraries(paperboat_tests PRIVATE paperboat::toolkit_optimization)
  target_compile_definitions(paperboat_tests PRIVATE PAPERBOAT_HAS_OPTIMIZATION)
endif()
if(TARGET paperboat_toolkit_opencv)
  target_link_libraries(paperboat_tests PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat_tests PRIVATE PAPERBOAT_HAS_OPENCV)
endif()
target_compile_definitions(paperboat_tests
  PRIVATE PAPERBOAT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
# `paperboat new` (tools/paperboat_new.cpp): only when the tools are built.
if(TARGET paperboat_new)
  target_sources(paperboat_tests PRIVATE new_test.cpp)
  target_link_libraries(paperboat_tests PRIVATE paperboat_new)
endif()

include(Catch)
catch_discover_tests(paperboat_tests)
