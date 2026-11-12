# Film Effects for Premiere Pro, built with the shared engine adapter (shared/engine/hosts/premiere,
# shared/engine/cmake/vv_premiere.cmake). Needs the Adobe SDKs (never in this repository).
#
# macOS (CPU + Metal GPU):
#   cmake -S . -B build-mac -DFE_BUILD_PLUGIN=ON \
#         -DAE_SDK_DIR="/path/to/AfterEffectsSDK/Examples" -DPR_SDK_DIR="/path/to/Premiere Pro SDK"
# Windows (CPU + CUDA GPU; needs the CUDA toolkit for the GPU part, otherwise CPU only):
#   cmake -S . -B build-win -A x64 -DFE_BUILD_PLUGIN=ON ^
#         -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples" -DPR_SDK_DIR="C:/Premiere Pro SDK"
include(${VV_ENGINE_DIR}/cmake/vv_premiere.cmake)
include(${VV_ENGINE_DIR}/cmake/vv_metal.cmake)

set(FE_GPU_DEFAULT OFF)
if(APPLE)
  set(FE_GPU_DEFAULT ON)
endif()
option(FE_WITH_GPU "Build the Metal GPU entry point (macOS only)" ${FE_GPU_DEFAULT})
if(FE_WITH_GPU AND NOT APPLE)
  message(FATAL_ERROR "FE_WITH_GPU is the Metal path (macOS); on Windows use FE_WITH_CUDA")
endif()

# CUDA GPU entry (Windows). On by default when a CUDA toolkit is found.
set(FE_CUDA_DEFAULT OFF)
if(WIN32)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    set(FE_CUDA_DEFAULT ON)
  endif()
endif()
if(DEFINED VV_WITH_CUDA)  # set by the shared CI workflows
  set(FE_CUDA_DEFAULT ${VV_WITH_CUDA})
endif()
option(FE_WITH_CUDA "Build the CUDA GPU entry point (Windows, needs the CUDA toolkit)" ${FE_CUDA_DEFAULT})

set(FE_PR_SOURCES src/effect/FilmEffectsEffect.cpp src/effect/FilmEffectsCuda.cpp)
set(FE_PR_ARGS)
if(FE_WITH_GPU)
  set(FE_PR_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/premiere/FilmEffects.metallib)
  vv_add_metallib(fe_pr_metallib SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/FilmEffects.metal
                  OUTPUT ${FE_PR_METALLIB} INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src/core
                  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common/fe_kernels.h)
  list(APPEND FE_PR_SOURCES src/effect/FilmEffectsMetal.mm src/gpu/metal/MetalFilmEngine.mm)
  list(APPEND FE_PR_ARGS GPU_METAL METALLIB ${FE_PR_METALLIB} METALLIB_TARGET fe_pr_metallib)
endif()
if(FE_WITH_CUDA)
  if(NOT WIN32)
    message(FATAL_ERROR "FE_WITH_CUDA targets Windows")
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fe_cuda_engine.cmake)
  list(APPEND FE_PR_ARGS GPU_CUDA)
  message(STATUS "Film Effects: CUDA GPU path enabled")
elseif(WIN32)
  message(STATUS "Film Effects: no CUDA toolkit found - Windows plugins will render on the CPU only")
endif()

vv_add_premiere_effect(FilmEffects PIPL src/effect/FilmEffectsPiPL.r BUNDLE_ID com.venostavisuals.FilmEffects
                       SOURCES ${FE_PR_SOURCES} LINK fe_core INCLUDES src/effect src/core src/gpu/metal
                       ${FE_PR_ARGS})
if(FE_WITH_CUDA)
  target_compile_definitions(FilmEffects PRIVATE FE_WITH_CUDA_RENDERER)
  target_link_libraries(FilmEffects PRIVATE fe_cuda_engine)
endif()
