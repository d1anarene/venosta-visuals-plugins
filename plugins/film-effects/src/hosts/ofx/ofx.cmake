# Film Effects OpenFX bundle (DaVinci Resolve and other OFX hosts) via the shared engine adapter
# (shared/engine/hosts/ofx, shared/engine/cmake/vv_ofx.cmake). Output: FilmEffects.ofx.bundle.
include(${VV_ENGINE_DIR}/cmake/vv_ofx.cmake)
include(${VV_ENGINE_DIR}/cmake/vv_metal.cmake)

set(VV_VERSION "${PROJECT_VERSION}" CACHE STRING "Version written to bundles (CI sets it)")
set(FE_OFX_VERSION "${VV_VERSION}" CACHE STRING "Version written to the bundle's Info.plist")
set(FE_OFX_SOURCES src/effect/FilmEffectsEffect.cpp src/effect/FilmEffectsCuda.cpp)
set(FE_OFX_METALLIB "")
set(FE_OFX_METALLIB_TARGET "")
if(APPLE)
  set(FE_OFX_METALLIB ${CMAKE_BINARY_DIR}/ofx/FilmEffects.metallib)
  set(FE_OFX_METALLIB_TARGET fe_ofx_metallib)
  vv_add_metallib(fe_ofx_metallib SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/FilmEffects.metal
                  OUTPUT ${FE_OFX_METALLIB} INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src/core
                  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common/fe_kernels.h)
  list(APPEND FE_OFX_SOURCES src/effect/FilmEffectsMetal.mm src/gpu/metal/MetalFilmEngine.mm)
endif()

vv_add_ofx_bundle(FilmEffectsOFX NAME FilmEffects DISPLAY_NAME "Film Effects"
                  BUNDLE_ID com.venostavisuals.FilmEffects.ofx VERSION ${FE_OFX_VERSION}
                  SOURCES ${FE_OFX_SOURCES} LINK fe_core
                  METALLIB ${FE_OFX_METALLIB} METALLIB_TARGET ${FE_OFX_METALLIB_TARGET}
                  EXPECT_ID com.venostavisuals.FilmEffects)
target_include_directories(FilmEffectsOFX PRIVATE src/effect src/core src/gpu/metal)

if(APPLE)
  target_link_libraries(FilmEffectsOFX PRIVATE "-framework Foundation" "-framework Metal")
else()
  # CUDA where a CUDA toolkit is found (Windows/Linux; optional).
  include(CheckLanguage)
  check_language(CUDA)
  set(FE_OFX_CUDA_DEFAULT OFF)
  if(CMAKE_CUDA_COMPILER)
    set(FE_OFX_CUDA_DEFAULT ON)
  endif()
  if(DEFINED VV_WITH_CUDA)  # set by the shared CI workflows
    set(FE_OFX_CUDA_DEFAULT ${VV_WITH_CUDA})
  endif()
  option(FE_OFX_WITH_CUDA "Build the CUDA GPU path of the OpenFX plug-in" ${FE_OFX_CUDA_DEFAULT})
  if(FE_OFX_WITH_CUDA)
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fe_cuda_engine.cmake)
    target_compile_definitions(FilmEffectsOFX PRIVATE FE_WITH_CUDA_RENDERER)
    target_link_libraries(FilmEffectsOFX PRIVATE fe_cuda_engine)
    message(STATUS "Film Effects OFX: CUDA GPU path enabled")
  else()
    message(STATUS "Film Effects OFX: no CUDA toolkit - CPU rendering only")
  endif()
endif()
