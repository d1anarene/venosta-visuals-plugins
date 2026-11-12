cmake_minimum_required(VERSION 3.20)
# macOS: universal binaries (Apple Silicon + Intel). Must be set before project().
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
project(LensDiffusion VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- shared engine (repository shared/engine) ----------------------------------------------------
if(NOT TARGET vv_core)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/engine ${CMAKE_BINARY_DIR}/vv-engine)
endif()

# ---- host-agnostic core -------------------------------------------------------------------------
add_library(ld_core STATIC src/core/lens_params.cpp src/core/lens.cpp)
target_include_directories(ld_core PUBLIC src/core)
target_link_libraries(ld_core PUBLIC vv_core)
set_target_properties(ld_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(ld_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ld_core_tests tests/core/test_main.cpp src/effect/LensEffect.cpp)
target_include_directories(ld_core_tests PRIVATE src/effect)
target_link_libraries(ld_core_tests PRIVATE ld_core)
add_executable(ld_render tools/ld_render.cpp)
target_link_libraries(ld_render PRIVATE ld_core)

enable_testing()
add_test(NAME core COMMAND ld_core_tests)

set(LD_EFFECT_SOURCES src/effect/LensEffect.cpp)
set(VV_VERSION "${PROJECT_VERSION}" CACHE STRING "Version written to bundles (CI sets it)")

# ---- Premiere Pro (needs the Adobe SDKs: -DLD_BUILD_PLUGIN=ON -DAE_SDK_DIR=... -DPR_SDK_DIR=...) ---
option(LD_BUILD_PLUGIN "Build the native Premiere plugin (requires Adobe SDKs)" OFF)
if(LD_BUILD_PLUGIN)
  include(${VV_ENGINE_DIR}/cmake/vv_premiere.cmake)
  vv_add_premiere_effect(LensDiffusion PIPL src/effect/LensPiPL.r BUNDLE_ID com.venostavisuals.LensDiffusion
                         SOURCES ${LD_EFFECT_SOURCES} LINK ld_core INCLUDES src/effect src/core)
endif()

# ---- OpenFX / DaVinci Resolve (no Adobe SDK; downloads the BSD OpenFX SDK) -----------------------
option(LD_BUILD_OFX "Build the OpenFX plug-in bundle" OFF)
if(LD_BUILD_OFX)
  include(${VV_ENGINE_DIR}/cmake/vv_ofx.cmake)
  vv_add_ofx_bundle(LensDiffusionOFX NAME LensDiffusion DISPLAY_NAME "Lens &amp; Diffusion"
                    BUNDLE_ID com.venostavisuals.LensDiffusion.ofx VERSION ${VV_VERSION}
                    SOURCES ${LD_EFFECT_SOURCES} LINK ld_core EXPECT_ID com.venostavisuals.LensDiffusion)
  target_include_directories(LensDiffusionOFX PRIVATE src/effect src/core)
endif()
