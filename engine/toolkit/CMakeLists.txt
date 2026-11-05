# Shared toolkit machinery. A package lives in src/<name>/ and is discovered from its
# toolkit.toml: its C++ sources are globbed, and its registration functions are gathered
# into one generated translation unit. Adding a package does not edit this file or registry.cpp.
set(PAPERBOAT_TOOLKIT_CORE_SOURCES
  src/block.cpp
  src/bases.cpp
  src/expression.cpp
  src/registry.cpp
  src/descriptor.cpp
  src/variables.cpp
  # For domain code: solvers, Model (a station object of ODEs), scene builders.
  src/solve.cpp
  src/model.cpp
  src/scene.cpp
)

file(GLOB toolkit_manifests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*/toolkit.toml")
list(SORT toolkit_manifests)
if(NOT toolkit_manifests)
  message(FATAL_ERROR "The toolkit has no src/<package>/toolkit.toml manifests")
endif()

set(package_registry_cpp "${CMAKE_CURRENT_BINARY_DIR}/package_registry.cpp")
file(WRITE "${package_registry_cpp}"
  "// Generated from the toolkit.toml files under engine/toolkit/src.\n"
  "#include <paperboat/toolkit/registry.hpp>\n\n")
set(package_registration_calls "")
set(package_definition_sources "")

foreach(manifest IN LISTS toolkit_manifests)
  file(STRINGS "${manifest}" name_line REGEX "^name[ \t]*=")
  file(STRINGS "${manifest}" register_line REGEX "^register[ \t]*=")
  if(NOT name_line OR NOT register_line)
    message(FATAL_ERROR "${manifest}: needs name and register fields")
  endif()
  string(REGEX REPLACE "^name[ \t]*=[ \t]*\"([A-Za-z_][A-Za-z0-9_]*)\"[ \t]*$" "\\1" package "${name_line}")
  if(NOT package MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "${manifest}: name must be a C++ package identifier")
  endif()
  string(REGEX REPLACE "^register[ \t]*=[ \t]*\\[(.*)\\][ \t]*$" "\\1" registrar_text "${register_line}")
  string(REPLACE "\"" "" registrar_text "${registrar_text}")
  string(REGEX REPLACE "[ \t]" "" registrar_text "${registrar_text}")
  string(REPLACE "," ";" registrars "${registrar_text}")
  list(FILTER registrars EXCLUDE REGEX "^$")
  if(NOT registrars)
    message(FATAL_ERROR "${manifest}: register must name at least one package function")
  endif()

  file(APPEND "${package_registry_cpp}" "namespace paperboat::toolkit::${package} {\n")
  foreach(function IN LISTS registrars)
    if(NOT function MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
      message(FATAL_ERROR "${manifest}: invalid registration function \"${function}\"")
    endif()
    file(APPEND "${package_registry_cpp}" "void ${function}(Registry&);\n")
    string(APPEND package_registration_calls "  ${package}::${function}(registry);\n")
  endforeach()
  file(APPEND "${package_registry_cpp}" "}  // namespace paperboat::toolkit::${package}\n\n")

  # A package may define blocks on a C++ parent in TOML. Embed those definitions in the
  # same library, using the package name for the symbol convention (signal -> kSignalBlocks).
  file(STRINGS "${manifest}" definitions_line REGEX "^definitions[ \t]*=")
  if(definitions_line)
    string(REGEX REPLACE "^definitions[ \t]*=[ \t]*\"([^\"]+)\"[ \t]*$" "\\1" definitions_file "${definitions_line}")
    set(definitions_path "${CMAKE_CURRENT_SOURCE_DIR}/src/${package}/${definitions_file}")
    if(NOT EXISTS "${definitions_path}")
      message(FATAL_ERROR "${manifest}: no definitions file ${definitions_file}")
    endif()
    file(READ "${definitions_path}" embedded_definitions)
    string(SUBSTRING "${package}" 0 1 package_first)
    string(TOUPPER "${package_first}" package_first)
    string(SUBSTRING "${package}" 1 -1 package_rest)
    set(PAPERBOAT_EMBED_SYMBOL "k${package_first}${package_rest}Blocks")
    set(PAPERBOAT_EMBED_CONTENT "${embedded_definitions}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${package}_blocks.cpp")
    configure_file(src/embed_package.cpp.in "${generated}" @ONLY)
    list(APPEND package_definition_sources "${generated}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${definitions_path}")
  endif()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
endforeach()

file(APPEND "${package_registry_cpp}" "namespace paperboat::toolkit {\n")
file(APPEND "${package_registry_cpp}" "void register_manifest_packages(Registry& registry) {\n")
file(APPEND "${package_registry_cpp}" "${package_registration_calls}")
file(APPEND "${package_registry_cpp}" "}\n}  // namespace paperboat::toolkit\n")

# New package source files are picked up on reconfigure. Manifests control registration;
# each listed function owns the definitions for its files. Similar blocks may share a file.
file(GLOB_RECURSE package_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp")
add_library(paperboat_toolkit ${PAPERBOAT_TOOLKIT_CORE_SOURCES} ${package_sources} ${package_definition_sources}
            "${package_registry_cpp}")
add_library(paperboat::toolkit ALIAS paperboat_toolkit)

target_include_directories(paperboat_toolkit PUBLIC include PRIVATE src)
target_link_libraries(paperboat_toolkit
  PUBLIC nlohmann_json::nlohmann_json  # variables.hpp takes the protocol's JSON
  PRIVATE tomlplusplus::tomlplusplus paperboat_warnings)
