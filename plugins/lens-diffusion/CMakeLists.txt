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

add_executable(ld_core_tests tests/core/test_main.cpp src/effect/LensEffect.cpp src/effect/LensCuda.cpp)
target_include_directories(ld_core_tests PRIVATE src/effect)
target_compile_definitions(ld_core_tests PRIVATE LD_NO_GPU_RENDERERS)
target_link_libraries(ld_core_tests PRIVATE ld_core)
add_executable(ld_render tools/ld_render.cpp)
target_link_libraries(ld_render PRIVATE ld_core)

# GPU pass sequence (shared by the CUDA and Metal backends) emulated on the CPU vs the reference.
add_executable(ld_gpu_emulation_tests tests/gpu/ld_gpu_emulation_test.cpp)
target_include_directories(ld_gpu_emulation_tests PRIVATE src/gpu/common)
target_link_libraries(ld_gpu_emulation_tests PRIVATE ld_core)

enable_testing()
add_test(NAME core COMMAND ld_core_tests)
add_test(NAME gpu_emulation COMMAND ld_gpu_emulation_tests)

set(VV_VERSION "${PROJECT_VERSION}" CACHE STRING "Version written to bundles (CI sets it)")

option(LD_BUILD_PLUGIN "Build the native Premiere plugin (requires Adobe SDKs)" OFF)
option(LD_BUILD_OFX "Build the OpenFX plug-in bundle (downloads the BSD OpenFX SDK)" OFF)
option(LD_BUILD_GPU_TESTS "Build the Metal-vs-CPU comparison (macOS)" OFF)

# ---- GPU renderers (used by both hosts) ----------------------------------------------------------
# macOS: Metal (always). Windows/Linux: CUDA when a CUDA toolkit is found (or -DVV_WITH_CUDA=ON).
set(LD_EFFECT_SOURCES src/effect/LensEffect.cpp src/effect/LensCuda.cpp)
set(LD_EFFECT_INCLUDES src/effect src/core src/gpu/common src/gpu/metal)
set(LD_GPU_LINK "")
set(LD_GPU_DEFS "")
set(LD_METALLIB "")
if(APPLE AND (LD_BUILD_PLUGIN OR LD_BUILD_OFX OR LD_BUILD_GPU_TESTS))
  enable_language(OBJCXX)
  include(${VV_ENGINE_DIR}/cmake/vv_metal.cmake)
  set(LD_METALLIB ${CMAKE_BINARY_DIR}/metal/LensDiffusion.metallib)
  vv_add_metallib(ld_metallib SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/LensDiffusion.metal
                  OUTPUT ${LD_METALLIB} INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src/core
                  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ld_kernels.h ${VV_ENGINE_DIR}/gpu/metal/vv_common.metal)
  add_library(ld_metal_engine STATIC src/gpu/metal/LensMetalEngine.mm ${VV_ENGINE_DIR}/gpu/metal/vv_metal_backend.mm)
  target_include_directories(ld_metal_engine PUBLIC src/gpu/metal ${VV_ENGINE_DIR}/gpu/metal PRIVATE src/gpu/common)
  target_link_libraries(ld_metal_engine PUBLIC ld_core "-framework Foundation" "-framework Metal")
  target_compile_options(ld_metal_engine PRIVATE -fobjc-arc)
  set_target_properties(ld_metal_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
  list(APPEND LD_EFFECT_SOURCES src/effect/LensMetal.mm)
  list(APPEND LD_GPU_LINK ld_metal_engine)
elseif(NOT APPLE AND (LD_BUILD_PLUGIN OR LD_BUILD_OFX))
  include(CheckLanguage)
  check_language(CUDA)
  set(LD_CUDA_DEFAULT OFF)
  if(CMAKE_CUDA_COMPILER)
    set(LD_CUDA_DEFAULT ON)
  endif()
  if(DEFINED VV_WITH_CUDA)  # set by the shared CI workflows
    set(LD_CUDA_DEFAULT ${VV_WITH_CUDA})
  endif()
  option(LD_WITH_CUDA "Build the CUDA renderer (needs the CUDA toolkit)" ${LD_CUDA_DEFAULT})
  if(LD_WITH_CUDA)
    include(cmake/ld_cuda_engine.cmake)
    list(APPEND LD_GPU_LINK ld_cuda_engine)
    list(APPEND LD_GPU_DEFS LD_WITH_CUDA_RENDERER)
    message(STATUS "Lens & Diffusion: CUDA renderer enabled")
  endif()
endif()

# Metal engine on a Mac GPU vs the CPU reference (CI: lens-diffusion-metal).
if(LD_BUILD_GPU_TESTS AND APPLE)
  add_executable(ld_metal_vs_cpu tests/gpu/ld_metal_vs_cpu.mm)
  target_link_libraries(ld_metal_vs_cpu PRIVATE ld_metal_engine)
  target_compile_options(ld_metal_vs_cpu PRIVATE -fobjc-arc)
  add_dependencies(ld_metal_vs_cpu ld_metallib)
endif()

# ---- Premiere Pro (needs the Adobe SDKs: -DLD_BUILD_PLUGIN=ON -DAE_SDK_DIR=... -DPR_SDK_DIR=...) ---
if(LD_BUILD_PLUGIN)
  include(${VV_ENGINE_DIR}/cmake/vv_premiere.cmake)
  set(LD_PR_ARGS)
  if(LD_METALLIB)
    list(APPEND LD_PR_ARGS GPU_METAL METALLIB ${LD_METALLIB} METALLIB_TARGET ld_metallib)
  endif()
  if(LD_WITH_CUDA)
    list(APPEND LD_PR_ARGS GPU_CUDA)
  endif()
  vv_add_premiere_effect(LensDiffusion PIPL src/effect/LensPiPL.r BUNDLE_ID com.venostavisuals.LensDiffusion
                         SOURCES ${LD_EFFECT_SOURCES} LINK ld_core ${LD_GPU_LINK} INCLUDES ${LD_EFFECT_INCLUDES}
                         ${LD_PR_ARGS})
  target_compile_definitions(LensDiffusion PRIVATE ${LD_GPU_DEFS})
endif()

# ---- OpenFX / DaVinci Resolve (no Adobe SDK; downloads the BSD OpenFX SDK) -----------------------
if(LD_BUILD_OFX)
  include(${VV_ENGINE_DIR}/cmake/vv_ofx.cmake)
  set(LD_OFX_METAL)
  if(LD_METALLIB)
    set(LD_OFX_METAL METALLIB ${LD_METALLIB} METALLIB_TARGET ld_metallib)
  endif()
  vv_add_ofx_bundle(LensDiffusionOFX NAME LensDiffusion DISPLAY_NAME "Lens &amp; Diffusion"
                    BUNDLE_ID com.venostavisuals.LensDiffusion.ofx VERSION ${VV_VERSION}
                    SOURCES ${LD_EFFECT_SOURCES} LINK ld_core ${LD_GPU_LINK} EXPECT_ID com.venostavisuals.LensDiffusion
                    ${LD_OFX_METAL})
  target_include_directories(LensDiffusionOFX PRIVATE ${LD_EFFECT_INCLUDES})
  target_compile_definitions(LensDiffusionOFX PRIVATE ${LD_GPU_DEFS})
endif()
