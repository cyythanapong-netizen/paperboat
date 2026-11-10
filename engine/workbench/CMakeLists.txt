# The workbench backend: the engine's standard toolkits with no domain, on the default port.
add_library(paperboat_workbench STATIC workbench.cpp)
target_include_directories(paperboat_workbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(paperboat_workbench PUBLIC paperboat::core PRIVATE paperboat_warnings)

add_executable(paperboat-workbench main.cpp)
target_link_libraries(paperboat-workbench PRIVATE paperboat_workbench paperboat_warnings)

if(PROJECT_IS_TOP_LEVEL)
  install(TARGETS paperboat-workbench RUNTIME DESTINATION bin COMPONENT runtime)
endif()
