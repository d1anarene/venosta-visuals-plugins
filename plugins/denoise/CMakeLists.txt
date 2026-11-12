cmake_minimum_required(VERSION 3.20)
project(Denoise VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TARGET vv_core)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/engine ${CMAKE_BINARY_DIR}/vv-engine)
endif()

# Research phase: CPU core + evaluation harness only (no host plug-in yet).
add_library(dn_core STATIC src/core/dn_pyramid.cpp src/core/dn_profile.cpp)
target_include_directories(dn_core PUBLIC src/core)
target_link_libraries(dn_core PUBLIC vv_core)
set_target_properties(dn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(dn_eval STATIC src/eval/dn_synth.cpp src/eval/dn_metrics.cpp)
target_include_directories(dn_eval PUBLIC src/eval)
target_link_libraries(dn_eval PUBLIC dn_core)

if(NOT MSVC)
  target_compile_options(dn_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(dn_eval PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(dn_core_tests tests/core/test_main.cpp)
target_link_libraries(dn_core_tests PRIVATE dn_eval)
add_executable(dn_eval_tool tools/dn_eval.cpp)
target_link_libraries(dn_eval_tool PRIVATE dn_eval)
enable_testing()
add_test(NAME core COMMAND dn_core_tests)
