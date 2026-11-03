add_executable(paperboat-catalog paperboat_catalog.cpp)
target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit nlohmann_json::nlohmann_json paperboat_warnings)
if(TARGET paperboat_toolkit_opencv)  # knows the optional packages that were built
  target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat-catalog PRIVATE PAPERBOAT_HAS_OPENCV)
endif()
