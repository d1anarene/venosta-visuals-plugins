# OpenFX plug-in bundle (DaVinci Resolve and other OFX hosts): FilmEffects.ofx.bundle with
# Film Effects, Film Bloom, Film Halation and Film Grain.
#
# The OpenFX headers and C++ Support library are BSD-3-Clause (Academy Software Foundation) and are
# fetched at configure time from GitHub at a pinned commit; point OFX_SDK_DIR at a local checkout
# to build offline. Builds on macOS (universal), Windows (x64) and Linux (x86-64).
#
# Output: ${CMAKE_BINARY_DIR}/FilmEffects.ofx.bundle/Contents/{Info.plist,<arch>/FilmEffects.ofx}
# Install: copy the bundle to
#   macOS   /Library/OFX/Plugins/
#   Windows C:\Program Files\Common Files\OFX\Plugins\
#   Linux   /usr/OFX/Plugins/

set(OFX_SDK_DIR "" CACHE PATH "OpenFX source checkout (empty = download the pinned commit)")
if(NOT OFX_SDK_DIR)
  include(FetchContent)
  FetchContent_Declare(openfx
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    GIT_TAG 4249fe1b590a7e65caf4b1e4693b6b53220703f5
    GIT_SHALLOW FALSE)
  FetchContent_GetProperties(openfx)
  if(NOT openfx_POPULATED)
    FetchContent_Populate(openfx)  # headers + sources only; OpenFX's own CMake project is not used
  endif()
  set(OFX_SDK_DIR ${openfx_SOURCE_DIR})
endif()
if(NOT EXISTS ${OFX_SDK_DIR}/include/ofxImageEffect.h)
  message(FATAL_ERROR "OpenFX headers not found in OFX_SDK_DIR=${OFX_SDK_DIR}")
endif()

file(GLOB FE_OFX_SUPPORT_SOURCES ${OFX_SDK_DIR}/Support/Library/*.cpp)
add_library(fe_ofx_support STATIC ${FE_OFX_SUPPORT_SOURCES})
target_include_directories(fe_ofx_support PUBLIC ${OFX_SDK_DIR}/include ${OFX_SDK_DIR}/Support/include
                                          PRIVATE ${OFX_SDK_DIR}/Support/Library)
set_target_properties(fe_ofx_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(fe_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_definitions(fe_ofx_support PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

add_library(FilmEffectsOFX MODULE src/hosts/ofx/FilmEffectsOFX.cpp src/hosts/ofx/FilmEffectsOFXCuda.cpp)
target_include_directories(FilmEffectsOFX PRIVATE src/hosts/ofx src/hosts/premiere src/core)
target_link_libraries(FilmEffectsOFX PRIVATE fe_ofx_support fe_core)

# GPU: Metal on macOS (always), CUDA where a CUDA toolkit is found (Windows/Linux; optional).
if(APPLE)
  enable_language(OBJCXX)
  set(FE_OFX_METALLIB ${CMAKE_BINARY_DIR}/ofx/FilmEffects.metallib)
  add_custom_command(OUTPUT ${FE_OFX_METALLIB}
    COMMAND xcrun -sdk macosx metal -fno-fast-math -I ${CMAKE_CURRENT_SOURCE_DIR}/src/core -I ${VV_ENGINE_DIR}/core
            -c ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/FilmEffects.metal -o ${CMAKE_BINARY_DIR}/ofx/FilmEffects.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_BINARY_DIR}/ofx/FilmEffects.air -o ${FE_OFX_METALLIB}
    DEPENDS src/gpu/metal/FilmEffects.metal src/core/common/fe_kernels.h ${VV_ENGINE_DIR}/core/vv/kernels.h)
  add_custom_target(fe_ofx_metallib DEPENDS ${FE_OFX_METALLIB})
  add_dependencies(FilmEffectsOFX fe_ofx_metallib)
  target_sources(FilmEffectsOFX PRIVATE src/hosts/ofx/FilmEffectsOFXMetal.mm src/gpu/metal/MetalFilmEngine.mm)
  target_include_directories(FilmEffectsOFX PRIVATE src/gpu/metal)
  target_compile_options(FilmEffectsOFX PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_libraries(FilmEffectsOFX PRIVATE "-framework Foundation" "-framework Metal")
else()
  include(CheckLanguage)
  check_language(CUDA)
  set(FE_OFX_CUDA_DEFAULT OFF)
  if(CMAKE_CUDA_COMPILER)
    set(FE_OFX_CUDA_DEFAULT ON)
  endif()
  option(FE_OFX_WITH_CUDA "Build the CUDA GPU path of the OpenFX plug-in" ${FE_OFX_CUDA_DEFAULT})
  if(FE_OFX_WITH_CUDA)
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fe_cuda_engine.cmake)
    target_compile_definitions(FilmEffectsOFX PRIVATE FE_OFX_CUDA)
    target_link_libraries(FilmEffectsOFX PRIVATE fe_cuda_engine)
    message(STATUS "Film Effects OFX: CUDA GPU path enabled")
  else()
    message(STATUS "Film Effects OFX: no CUDA toolkit - CPU rendering only")
  endif()
endif()
set_target_properties(FilmEffectsOFX PROPERTIES PREFIX "" SUFFIX ".ofx" OUTPUT_NAME FilmEffects
                                                CXX_VISIBILITY_PRESET hidden)

if(APPLE)
  set(FE_OFX_ARCH MacOS)
elseif(WIN32 AND CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64[Ee][Cc]$")
  set(FE_OFX_ARCH Win-arm64ec)  # OpenFX 1.5: Resolve on Windows-on-Arm is an Arm64EC host
elseif(WIN32)
  set(FE_OFX_ARCH Win64)
else()
  set(FE_OFX_ARCH Linux-x86-64)
endif()
set(FE_OFX_BUNDLE ${CMAKE_BINARY_DIR}/FilmEffects.ofx.bundle)
set(FE_OFX_VERSION "${PROJECT_VERSION}" CACHE STRING "Version written to the bundle's Info.plist")
configure_file(src/hosts/ofx/Info.plist.in ${CMAKE_BINARY_DIR}/ofx/Info.plist @ONLY)
add_custom_command(TARGET FilmEffectsOFX POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${FE_OFX_BUNDLE}/Contents/${FE_OFX_ARCH}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:FilmEffectsOFX> ${FE_OFX_BUNDLE}/Contents/${FE_OFX_ARCH}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/ofx/Info.plist ${FE_OFX_BUNDLE}/Contents/Info.plist
  VERBATIM)
if(APPLE)
  add_custom_command(TARGET FilmEffectsOFX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FE_OFX_BUNDLE}/Contents/Resources
    COMMAND ${CMAKE_COMMAND} -E copy ${FE_OFX_METALLIB} ${FE_OFX_BUNDLE}/Contents/Resources/
    VERBATIM)
endif()

# Load test: dlopen the plug-in like a host and check the four effects are exported.
add_executable(fe_ofx_load_test tests/ofx/ofx_load_test.cpp)
target_include_directories(fe_ofx_load_test PRIVATE ${OFX_SDK_DIR}/include)
if(UNIX)
  target_link_libraries(fe_ofx_load_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(fe_ofx_load_test FilmEffectsOFX)
add_test(NAME ofx_load COMMAND fe_ofx_load_test ${FE_OFX_BUNDLE}/Contents/${FE_OFX_ARCH}/FilmEffects.ofx)
