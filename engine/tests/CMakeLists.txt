# An install tree carries no optional toolkit (pb#297): the release guard, runnable with ctest.
find_program(PAPERBOAT_TEST_PYTHON NAMES python3 python)
if(PAPERBOAT_TEST_PYTHON)
  add_test(NAME bundle_no_optional_toolkits
    COMMAND ${CMAKE_COMMAND}
      -DBUILD_DIR=${CMAKE_BINARY_DIR}
      -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../..
      -DPYTHON=${PAPERBOAT_TEST_PYTHON}
      -DCONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/bundle_test.cmake)
  set_tests_properties(bundle_no_optional_toolkits PROPERTIES LABELS bundle)
else()
  message(STATUS "python3 not found: the install-tree bundle check is off")
endif()

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
  toolkit_install_test.cpp
  solve_test.cpp
  interactive_test.cpp
  step_test.cpp
  station_test.cpp
  core_test.cpp
  record_test.cpp
  cache_test.cpp
  events_test.cpp
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
# The workbench is a standalone-engine target (engine/CMakeLists.txt); test it where it exists.
if(TARGET paperboat_workbench)
  target_sources(paperboat_tests PRIVATE workbench_test.cpp)
  target_link_libraries(paperboat_tests PRIVATE paperboat_workbench)
endif()
if(TARGET paperboat_toolkit_optimization)
  target_sources(paperboat_tests PRIVATE optimization_test.cpp)
  target_link_libraries(paperboat_tests PRIVATE paperboat::toolkit_optimization)
  target_compile_definitions(paperboat_tests PRIVATE PAPERBOAT_HAS_OPTIMIZATION)
endif()
if(TARGET paperboat_toolkit_opencv)
  target_sources(paperboat_tests PRIVATE opencv_test.cpp)
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
