add_executable(paperboat_tests
  descriptor_test.cpp
  control_test.cpp
  solve_test.cpp
  interactive_test.cpp
  step_test.cpp
  station_test.cpp
  core_test.cpp
  variables_test.cpp
  connection_test.cpp
  cli_test.cpp
)
target_link_libraries(paperboat_tests
  PRIVATE paperboat::toolkit paperboat::core nlohmann_json::nlohmann_json Catch2::Catch2WithMain paperboat_warnings)
target_compile_definitions(paperboat_tests
  PRIVATE PAPERBOAT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(Catch)
catch_discover_tests(paperboat_tests)
