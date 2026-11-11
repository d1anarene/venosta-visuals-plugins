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

add_library(FilmEffectsOFX MODULE src/hosts/ofx/FilmEffectsOFX.cpp)
target_include_directories(FilmEffectsOFX PRIVATE src/hosts/premiere src/core)
target_link_libraries(FilmEffectsOFX PRIVATE fe_ofx_support fe_core)
set_target_properties(FilmEffectsOFX PROPERTIES PREFIX "" SUFFIX ".ofx" OUTPUT_NAME FilmEffects
                                                CXX_VISIBILITY_PRESET hidden)

if(APPLE)
  set(FE_OFX_ARCH MacOS)
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

# Load test: dlopen the plug-in like a host and check the four effects are exported.
add_executable(fe_ofx_load_test tests/ofx/ofx_load_test.cpp)
target_include_directories(fe_ofx_load_test PRIVATE ${OFX_SDK_DIR}/include)
if(UNIX)
  target_link_libraries(fe_ofx_load_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(fe_ofx_load_test FilmEffectsOFX)
add_test(NAME ofx_load COMMAND fe_ofx_load_test ${FE_OFX_BUNDLE}/Contents/${FE_OFX_ARCH}/FilmEffects.ofx)
