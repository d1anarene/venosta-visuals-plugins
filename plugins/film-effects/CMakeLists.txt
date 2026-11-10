cmake_minimum_required(VERSION 3.20)
project(FilmEffects VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- host-agnostic core (always built) --------------------------------------------------------
add_library(fe_core STATIC
  src/core/params/params.cpp
  src/core/color/pixel_io.cpp
  src/core/blur/blur.cpp
  src/core/blur/blur_plan.cpp
  src/core/bloom/bloom.cpp
  src/core/halation/halation.cpp
  src/core/grain/grain.cpp
  src/core/pipeline/pipeline.cpp)
target_include_directories(fe_core PUBLIC src/core)
if(NOT MSVC)
  target_compile_options(fe_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(fe_core_tests tests/core/test_main.cpp)
target_link_libraries(fe_core_tests PRIVATE fe_core)
target_include_directories(fe_core_tests PRIVATE src/hosts/premiere)

add_executable(fe_render tools/fe_render.cpp)
target_link_libraries(fe_render PRIVATE fe_core)

enable_testing()
add_test(NAME core COMMAND fe_core_tests)

# ---- native Premiere plugin (macOS, needs the Adobe SDKs) --------------------------------------
# Configure with: -DAE_SDK_DIR=/path/to/AfterEffectsSDK/Examples -DPR_SDK_DIR=/path/to/Premiere SDK
option(FE_BUILD_PLUGIN "Build the native Premiere plugin (requires Adobe SDKs)" OFF)
if(FE_BUILD_PLUGIN)
  include(src/hosts/premiere/plugin.cmake)
endif()
