add_executable(paperboat-catalog paperboat_catalog.cpp)
target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit nlohmann_json::nlohmann_json paperboat_warnings)
if(TARGET paperboat_toolkit_opencv)  # knows the optional packages that were built
  target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat-catalog PRIVATE PAPERBOAT_HAS_OPENCV)
endif()

add_executable(paperboat-toolkit paperboat_toolkit.cpp)
target_link_libraries(paperboat-toolkit PRIVATE paperboat::toolkit paperboat_warnings)
if(TARGET paperboat_toolkit_opencv)
  target_link_libraries(paperboat-toolkit PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat-toolkit PRIVATE PAPERBOAT_HAS_OPENCV)
endif()

add_executable(paperboat paperboat_cli.cpp)
target_compile_definitions(paperboat PRIVATE PAPERBOAT_VERSION="${PROJECT_VERSION}")
if(PAPERBOAT_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(paperboat PRIVATE -static-libstdc++ -static-libgcc)
endif()
target_link_libraries(paperboat PRIVATE paperboat::core paperboat::toolkit nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus paperboat::warnings)
if(TARGET paperboat_toolkit_opencv)
  target_link_libraries(paperboat PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat PRIVATE PAPERBOAT_HAS_OPENCV)
endif()

# Installed layout: bin/paperboat and share/paperboat/{toolkits.toml,apps.toml}. The binary finds
# the registries at ../share/paperboat (toolkit::find_repo_root), so a package is relocatable.
if(PROJECT_IS_TOP_LEVEL)
  install(TARGETS paperboat RUNTIME DESTINATION bin COMPONENT runtime)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../toolkits.toml ${CMAKE_CURRENT_SOURCE_DIR}/../../apps.toml
          DESTINATION share/paperboat COMPONENT runtime)
  # paperboat check's smoke and golden stages run these, from <root>/protocol (pb#177).
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/smoke_test.py ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/golden.py
          DESTINATION share/paperboat/protocol COMPONENT runtime)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/schema/paperboat-0.schema.json
          DESTINATION share/paperboat/protocol/schema COMPONENT runtime)
endif()
