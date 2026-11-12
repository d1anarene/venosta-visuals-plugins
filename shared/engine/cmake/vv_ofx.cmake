# OpenFX bundles (DaVinci Resolve and other OFX hosts) for any plugin built on vv::Effect.
#
# The OpenFX headers and C++ Support library are BSD-3-Clause (Academy Software Foundation) and are
# fetched at configure time from GitHub at a pinned commit; set OFX_SDK_DIR to a local checkout to
# build offline. macOS (universal), Windows x64 / Arm64EC, Linux x86-64.
#
#   vv_add_ofx_bundle(<target> NAME <BundleName> DISPLAY_NAME "<Name>" BUNDLE_ID <reverse.dns>
#                     VERSION <x.y.z> SOURCES <effect sources...> LINK <libs...>
#                     [METALLIB <file.metallib> METALLIB_TARGET <target>] [EXPECT_ID <ofx id>])
#
# Output: ${CMAKE_BINARY_DIR}/<BundleName>.ofx.bundle/Contents/{Info.plist,<arch>/<BundleName>.ofx,
# Resources/<metallib>}. Install into /Library/OFX/Plugins, C:\Program Files\Common Files\OFX\Plugins
# or /usr/OFX/Plugins. With EXPECT_ID a ctest loads the bundle like a host and checks the identifier.
include_guard(GLOBAL)

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
  set(OFX_SDK_DIR ${openfx_SOURCE_DIR} CACHE PATH "" FORCE)
endif()
if(NOT EXISTS ${OFX_SDK_DIR}/include/ofxImageEffect.h)
  message(FATAL_ERROR "OpenFX headers not found in OFX_SDK_DIR=${OFX_SDK_DIR}")
endif()

if(NOT TARGET vv_ofx_support)
  file(GLOB VV_OFX_SUPPORT_SOURCES ${OFX_SDK_DIR}/Support/Library/*.cpp)
  add_library(vv_ofx_support STATIC ${VV_OFX_SUPPORT_SOURCES})
  target_include_directories(vv_ofx_support PUBLIC ${OFX_SDK_DIR}/include ${OFX_SDK_DIR}/Support/include
                                            PRIVATE ${OFX_SDK_DIR}/Support/Library)
  set_target_properties(vv_ofx_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_definitions(vv_ofx_support PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
  endif()
endif()

if(APPLE)
  enable_language(OBJCXX)  # directory scope (not allowed inside a function); Metal renderers are .mm
endif()

if(APPLE)
  set(VV_OFX_ARCH MacOS)
elseif(WIN32 AND CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64[Ee][Cc]$")
  set(VV_OFX_ARCH Win-arm64ec)  # OpenFX 1.5: Resolve on Windows-on-Arm is an Arm64EC host
elseif(WIN32)
  set(VV_OFX_ARCH Win64)
else()
  set(VV_OFX_ARCH Linux-x86-64)
endif()

if(NOT TARGET vv_ofx_load_test)
  add_executable(vv_ofx_load_test ${VV_ENGINE_DIR}/tests/ofx_load_test.cpp)
  target_include_directories(vv_ofx_load_test PRIVATE ${OFX_SDK_DIR}/include)
  if(UNIX)
    target_link_libraries(vv_ofx_load_test PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()

function(vv_add_ofx_bundle target)
  cmake_parse_arguments(A "" "NAME;DISPLAY_NAME;BUNDLE_ID;VERSION;METALLIB;METALLIB_TARGET;EXPECT_ID" "SOURCES;LINK" ${ARGN})
  add_library(${target} MODULE ${VV_ENGINE_DIR}/hosts/ofx/vv_ofx_effect.cpp ${A_SOURCES})
  target_link_libraries(${target} PRIVATE vv_ofx_support vv_core ${A_LINK})
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".ofx" OUTPUT_NAME ${A_NAME}
                                              CXX_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
  if(APPLE)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  endif()
  set(bundle ${CMAKE_BINARY_DIR}/${A_NAME}.ofx.bundle)
  set(VV_BUNDLE_NAME ${A_NAME})
  set(VV_DISPLAY_NAME ${A_DISPLAY_NAME})
  set(VV_BUNDLE_ID ${A_BUNDLE_ID})
  set(VV_VERSION ${A_VERSION})
  configure_file(${VV_ENGINE_DIR}/hosts/ofx/Info.plist.in ${CMAKE_BINARY_DIR}/${target}-Info.plist @ONLY)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${bundle}/Contents/${VV_OFX_ARCH}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${bundle}/Contents/${VV_OFX_ARCH}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${target}-Info.plist ${bundle}/Contents/Info.plist
    VERBATIM)
  if(A_METALLIB)
    add_dependencies(${target} ${A_METALLIB_TARGET})
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${bundle}/Contents/Resources
      COMMAND ${CMAKE_COMMAND} -E copy ${A_METALLIB} ${bundle}/Contents/Resources/
      VERBATIM)
  endif()
  if(A_EXPECT_ID)
    add_dependencies(vv_ofx_load_test ${target})
    add_test(NAME ${target}_ofx_load
             COMMAND vv_ofx_load_test ${bundle}/Contents/${VV_OFX_ARCH}/${A_NAME}.ofx ${A_EXPECT_ID})
  endif()
endfunction()
