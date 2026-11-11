# Shared toolkit machinery. A package lives in src/<name>/ and is discovered from its
# toolkit.toml: its C++ sources are globbed, and its registration functions are gathered
# into one generated translation unit. Adding a package does not edit this file or registry.cpp.
# One block, one folder: src/<name>/<block>/<block>.cpp defines paperboat::toolkit::<name>::add_<block>(Registry&),
# found by the folder's name, so adding a block edits no list. The block's folder also holds whatever else
# makes it work: its icon, its web/ (instruments, CSS, tests), its design. A `definitions` TOML file is
# registered last.
# (A manifest with an explicit `register = [...]` list keeps that list instead.)
# A manifest's `flows = ["signal"]` is the flow types its blocks work in (#9), for each block
# that names none of its own; without it, they work in every flow type.
set(PAPERBOAT_TOOLKIT_CORE_SOURCES
  src/block.cpp
  src/bases.cpp
  src/expression.cpp
  src/registry.cpp
  src/descriptor.cpp
  src/variables.cpp
  src/installer.cpp
  src/apps.cpp
  src/platform.cpp
  src/update.cpp
  # For domain code: solvers, Model (a station object of ODEs), scene builders.
  src/solve_param.cpp
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
  file(STRINGS "${manifest}" definitions_line REGEX "^definitions[ \t]*=")
  file(STRINGS "${manifest}" flows_line REGEX "^flows[ \t]*=")
  if(NOT name_line)
    message(FATAL_ERROR "${manifest}: needs a name field")
  endif()
  string(REGEX REPLACE "^name[ \t]*=[ \t]*\"([A-Za-z_][A-Za-z0-9_]*)\"[ \t]*$" "\\1" package "${name_line}")
  if(NOT package MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "${manifest}: name must be a C++ package identifier")
  endif()
  if(register_line)
    string(REGEX REPLACE "^register[ \t]*=[ \t]*\\[(.*)\\][ \t]*$" "\\1" registrar_text "${register_line}")
    string(REPLACE "\"" "" registrar_text "${registrar_text}")
    string(REGEX REPLACE "[ \t]" "" registrar_text "${registrar_text}")
    string(REPLACE "," ";" registrars "${registrar_text}")
    list(FILTER registrars EXCLUDE REGEX "^$")
  else()
    # One add_<block> per block folder src/<package>/<block>/<block>.cpp (other folders and files are not blocks).
    file(GLOB block_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${package}/*/*.cpp")
    list(SORT block_files)
    set(registrars "")
    foreach(block_file IN LISTS block_files)
      get_filename_component(stem "${block_file}" NAME_WE)
      get_filename_component(block_dir "${block_file}" DIRECTORY)
      get_filename_component(block_dir_name "${block_dir}" NAME)
      if(stem STREQUAL block_dir_name)
        list(APPEND registrars "add_${stem}")
      endif()
    endforeach()
    if(definitions_line)
      list(APPEND registrars "add_definitions")
    endif()
  endif()
  if(NOT registrars)
    message(FATAL_ERROR "${manifest}: no block folders (src/${package}/<block>/<block>.cpp) and no register list")
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
  if(flows_line)
    if(NOT flows_line MATCHES "^flows[ \t]*=[ \t]*\\[[ \t]*(\"[a-z]+\"[ \t]*,?[ \t]*)+\\][ \t]*(#.*)?$")
      message(FATAL_ERROR "${manifest}: flows must be a list of flow types on one line, such as flows = [\"signal\"]")
    endif()
    string(REGEX REPLACE "^flows[ \t]*=[ \t]*\\[(.*)\\][ \t]*(#.*)?$" "\\1" flows_text "${flows_line}")
    string(REGEX REPLACE "[ \t]" "" flows_text "${flows_text}")
    string(REGEX REPLACE ",$" "" flows_text "${flows_text}")
    # Checked (known flow types, none twice) when the registry is built: Registry::default_flows.
    string(APPEND package_registration_calls "  registry.default_flows(\"${package}\", {${flows_text}});\n")
  endif()

  # A package may define blocks on a C++ parent in TOML. Embed those definitions in the
  # same library, using the package name for the symbol convention (signal -> kSignalBlocks).
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
    set(PAPERBOAT_EMBED_PACKAGE "${package}")
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
# The solvers as their own library: no toolkit types, only the state, the derivative and the
# options. A native tool or another backend can link paperboat::solvers without the toolkit.
set(PAPERBOAT_SOLVER_SOURCES src/solve.cpp)
if(PAPERBOAT_WITH_SUNDIALS)
  list(APPEND PAPERBOAT_SOLVER_SOURCES src/implicit.cpp)
endif()
add_library(paperboat_solvers STATIC ${PAPERBOAT_SOLVER_SOURCES})
add_library(paperboat::solvers ALIAS paperboat_solvers)
target_include_directories(paperboat_solvers
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(paperboat_solvers PROPERTIES EXPORT_NAME solvers)
if(PAPERBOAT_WITH_SUNDIALS)
  target_link_libraries(paperboat_solvers PRIVATE SUNDIALS::cvode SUNDIALS::ida paperboat_warnings)
  set_property(SOURCE src/solve.cpp APPEND PROPERTY COMPILE_DEFINITIONS PAPERBOAT_WITH_SUNDIALS)
endif()

add_library(paperboat_toolkit ${PAPERBOAT_TOOLKIT_CORE_SOURCES} ${package_sources} ${package_definition_sources}
            "${package_registry_cpp}")
add_library(paperboat::toolkit ALIAS paperboat_toolkit)

target_include_directories(paperboat_toolkit
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE src)
# Third-party headers the built-in blocks compile in. SYSTEM: their warnings are not ours.
target_include_directories(paperboat_toolkit SYSTEM PRIVATE
  $<BUILD_INTERFACE:${boost_math_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${pocketfft_include}>)
target_compile_definitions(paperboat_toolkit PRIVATE BOOST_MATH_STANDALONE=1)
set_target_properties(paperboat_toolkit PROPERTIES EXPORT_NAME toolkit)
target_link_libraries(paperboat_toolkit
  PUBLIC nlohmann_json::nlohmann_json paperboat::solvers  # variables.hpp takes the protocol's JSON
  PRIVATE $<BUILD_INTERFACE:tomlplusplus::tomlplusplus> paperboat_warnings)
