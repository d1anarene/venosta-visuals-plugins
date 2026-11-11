cmake_minimum_required(VERSION 3.20)
# macOS: build everything (core library + plugins) as universal binaries, Apple Silicon + Intel.
# Must be set before project(); ignored on other platforms.
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
project(FilmEffects VERSION 1.0.0 LANGUAGES CXX)

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

# GPU pass sequence (shared by the CUDA backend) emulated on the CPU and compared with the reference.
add_executable(fe_gpu_emulation_tests tests/gpu/gpu_emulation_test.cpp)
target_include_directories(fe_gpu_emulation_tests PRIVATE src tools)
target_link_libraries(fe_gpu_emulation_tests PRIVATE fe_core)

enable_testing()
add_test(NAME core COMMAND fe_core_tests)
add_test(NAME gpu_emulation COMMAND fe_gpu_emulation_tests)

# ---- CUDA engine (NVIDIA, no Adobe SDK needed) --------------------------------------------------
# Compiles the CUDA kernels + engine into a static library. Used by the Windows plugin build and by
# CI to check that the CUDA code compiles with nvcc. Needs the CUDA toolkit (nvcc).
option(FE_BUILD_CUDA_ENGINE "Build the CUDA engine library (requires the CUDA toolkit)" OFF)
if(FE_BUILD_CUDA_ENGINE)
  include(cmake/fe_cuda_engine.cmake)
endif()

# ---- native Premiere plugins (macOS / Windows, need the Adobe SDKs) --------------------------------------
# Configure with: -DAE_SDK_DIR=/path/to/AfterEffectsSDK/Examples -DPR_SDK_DIR=/path/to/Premiere SDK
option(FE_BUILD_PLUGIN "Build the native Premiere plugin (requires Adobe SDKs)" OFF)
if(FE_BUILD_PLUGIN)
  include(src/hosts/premiere/plugin.cmake)
endif()

# ---- Metal backend compile + GPU-vs-CPU comparison (macOS, no Adobe SDK needed) ---------------
option(FE_BUILD_GPU_TESTS "Build the Metal engine and the GPU-vs-CPU comparison tool" OFF)
if(FE_BUILD_GPU_TESTS)
  if(NOT APPLE)
    message(FATAL_ERROR "FE_BUILD_GPU_TESTS requires macOS")
  endif()
  enable_language(OBJCXX)
  set(FE_METALLIB_TEST ${CMAKE_CURRENT_BINARY_DIR}/FilmEffects.metallib)
  add_custom_command(OUTPUT ${FE_METALLIB_TEST}
    COMMAND xcrun -sdk macosx metal -fno-fast-math -I ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            -c ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/FilmEffects.metal -o ${CMAKE_CURRENT_BINARY_DIR}/FilmEffects.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_CURRENT_BINARY_DIR}/FilmEffects.air -o ${FE_METALLIB_TEST}
    DEPENDS src/gpu/metal/FilmEffects.metal src/core/common/fe_kernels.h)
  add_custom_target(fe_metallib ALL DEPENDS ${FE_METALLIB_TEST})
  add_executable(fe_metal_vs_cpu tests/gpu/metal_vs_cpu.mm src/gpu/metal/MetalFilmEngine.mm)
  target_include_directories(fe_metal_vs_cpu PRIVATE src/gpu/metal tools)
  target_compile_options(fe_metal_vs_cpu PRIVATE -fobjc-arc)
  target_link_libraries(fe_metal_vs_cpu PRIVATE fe_core "-framework Foundation" "-framework Metal")
  add_dependencies(fe_metal_vs_cpu fe_metallib)
endif()
