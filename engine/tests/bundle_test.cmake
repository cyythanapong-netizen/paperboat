# Optional toolkits never enter an install tree (pb#297). ctest installs this build into a scratch
# prefix and hands it to scripts/check_bundle.py, so a stray install() fails locally and not in a
# release. The engine is Apache-2.0; a toolkit installed from engine/toolkit/<name> carries its own
# licence (the mesh toolkit is GPL-3.0), so one file in an artifact relicences the artifact.
if(NOT DEFINED BUILD_DIR OR NOT DEFINED ROOT OR NOT DEFINED PYTHON)
  message(FATAL_ERROR "bundle_test.cmake needs BUILD_DIR, ROOT and PYTHON")
endif()

set(prefix "${BUILD_DIR}/bundle-install")
file(REMOVE_RECURSE "${prefix}")

set(config_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  list(APPEND config_args --config "${CONFIG}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}" ${config_args}
  RESULT_VARIABLE install_code OUTPUT_VARIABLE install_out ERROR_VARIABLE install_err)
if(NOT install_code EQUAL 0)
  message(FATAL_ERROR "cmake --install failed:\n${install_out}\n${install_err}")
endif()

execute_process(
  COMMAND "${PYTHON}" "${ROOT}/scripts/check_bundle.py" --root "${ROOT}" --prefix "${prefix}"
  RESULT_VARIABLE check_code)
if(NOT check_code EQUAL 0)
  message(FATAL_ERROR "an optional toolkit is in the install tree; optional toolkits are never bundled (pb#297)")
endif()
