add_executable(paperboat-catalog paperboat_catalog.cpp)
target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit nlohmann_json::nlohmann_json paperboat_warnings)
if(TARGET paperboat_toolkit_opencv)  # knows the optional packages that were built
  target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat-catalog PRIVATE PAPERBOAT_HAS_OPENCV)
endif()
if(TARGET paperboat_toolkit_optimization)
  target_link_libraries(paperboat-catalog PRIVATE paperboat::toolkit_optimization)
  target_compile_definitions(paperboat-catalog PRIVATE PAPERBOAT_HAS_OPTIMIZATION)
endif()

add_executable(paperboat-toolkit paperboat_toolkit.cpp)
target_link_libraries(paperboat-toolkit PRIVATE paperboat::toolkit paperboat_warnings)
if(TARGET paperboat_toolkit_opencv)
  target_link_libraries(paperboat-toolkit PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat-toolkit PRIVATE PAPERBOAT_HAS_OPENCV)
endif()
if(TARGET paperboat_toolkit_optimization)
  target_link_libraries(paperboat-toolkit PRIVATE paperboat::toolkit_optimization)
  target_compile_definitions(paperboat-toolkit PRIVATE PAPERBOAT_HAS_OPTIMIZATION)
endif()

# `paperboat new`: the scaffolder and the templates it writes, embedded (new_templates.cmake).
# A library of its own so the tests call it too; never installed (linked into the binary).
include(new_templates.cmake)
paperboat_embed_templates(${CMAKE_CURRENT_BINARY_DIR}/new_templates.cpp)
add_library(paperboat_new STATIC paperboat_new.cpp ${CMAKE_CURRENT_BINARY_DIR}/new_templates.cpp)
target_include_directories(paperboat_new PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(paperboat_new PRIVATE paperboat_warnings)

add_executable(paperboat paperboat_cli.cpp)
target_compile_definitions(paperboat PRIVATE PAPERBOAT_VERSION="${PAPERBOAT_VERSION}")
if(PAPERBOAT_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(paperboat PRIVATE -static-libstdc++ -static-libgcc)
endif()
target_link_libraries(paperboat PRIVATE paperboat_new paperboat::core paperboat::toolkit nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus paperboat::warnings)
if(TARGET paperboat_toolkit_opencv)
  target_link_libraries(paperboat PRIVATE paperboat::toolkit_opencv)
  target_compile_definitions(paperboat PRIVATE PAPERBOAT_HAS_OPENCV)
endif()
if(TARGET paperboat_toolkit_optimization)
  target_link_libraries(paperboat PRIVATE paperboat::toolkit_optimization)
  target_compile_definitions(paperboat PRIVATE PAPERBOAT_HAS_OPTIMIZATION)
endif()

# Installed layout: bin/paperboat and share/paperboat/{toolkits.toml,apps.toml,web/,cli/}. The binary finds
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
  # The web app and the console, when built first (npm run build:release; scripts/build_release.sh
  # does): `paperboat start` serves share/paperboat/web, and the prompt is share/paperboat/cli/main.js.
  set(web_dist ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/web/dist)
  if(EXISTS ${web_dist}/index.html)
    install(DIRECTORY ${web_dist}/ DESTINATION share/paperboat/web COMPONENT runtime)
  endif()
  set(cli_bundle ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/cli/bundle/main.js)
  if(EXISTS ${cli_bundle})
    install(FILES ${cli_bundle} DESTINATION share/paperboat/cli COMPONENT runtime)
  endif()
  # Python backends (pb#223): the `paperboat` package in lib/paperboat/python, which the CLI puts on
  # PYTHONPATH, so `import paperboat` works with no pip step. Its extension is built from python/
  # (scripts/build_python.sh): one module per CPython ABI (abi3 for 3.12 and later, and 3.11);
  # each interpreter loads the one for it.
  set(PAPERBOAT_PYTHON_MODULES_DIR "" CACHE PATH "A folder of built paperboat._engine modules (_engine.*) to ship")
  if(PAPERBOAT_PYTHON_MODULES_DIR)
    file(GLOB py_modules "${PAPERBOAT_PYTHON_MODULES_DIR}/_engine.*")
    if(NOT py_modules)
      message(FATAL_ERROR "PAPERBOAT_PYTHON_MODULES_DIR: no _engine.* module in ${PAPERBOAT_PYTHON_MODULES_DIR}")
    endif()
    file(GLOB py_sources "${CMAKE_CURRENT_SOURCE_DIR}/../../python/paperboat/*.py")
    install(FILES ${py_sources} ${py_modules} DESTINATION lib/paperboat/python/paperboat COMPONENT runtime)
  endif()
endif()
