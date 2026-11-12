# Premiere Pro / After Effects effects (After Effects effect API + Premiere GPU extension) for any
# plugin built on vv::Effect. Needs the Adobe SDKs (never in this repository):
#   -DAE_SDK_DIR=<AfterEffectsSDK/Examples> -DPR_SDK_DIR=<Premiere Pro SDK root>
#
#   vv_add_premiere_effect(<Target> PIPL <plugin.r> BUNDLE_ID <reverse.dns>
#                          SOURCES <effect sources...> LINK <libs...> INCLUDES <dirs...>
#                          [GPU_METAL METALLIB <file.metallib> METALLIB_TARGET <target>] [GPU_CUDA])
#
# The target name is the binary name: <Target>.plugin (macOS bundle, universal) or <Target>.aex
# (Windows DLL). The PiPL is compiled with Rez (macOS) or cl + the SDK's PiPLtool (Windows), the
# way the SDK samples do it. Also defines vv_aex_load_test (Windows) once.
include_guard(GLOBAL)
if(NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "Premiere Pro effects build on macOS and Windows only")
endif()

set(AE_SDK_DIR "" CACHE PATH "After Effects SDK 'Examples' folder")
set(PR_SDK_DIR "" CACHE PATH "Premiere Pro SDK root folder")
set(AE_SDK_HEADERS "${AE_SDK_DIR}/Headers" CACHE PATH "")
set(AE_SDK_UTIL "${AE_SDK_DIR}/Util" CACHE PATH "")
set(AE_SDK_RESOURCES "${AE_SDK_DIR}/Resources" CACHE PATH "Folder with AE_General.r (and PiPLtool.exe on Windows)")
set(PR_SDK_HEADERS "${PR_SDK_DIR}/Examples/Headers" CACHE PATH "")
set(PR_SDK_UTILS "${PR_SDK_DIR}/Examples/Utils" CACHE PATH "")
set(PR_GPU_UTILS "${PR_SDK_DIR}/Examples/Projects/GPUVideoFilter/Utils" CACHE PATH "Folder with PrGPUFilterModule.h")
set(VV_PREMIERE_DIR ${VV_ENGINE_DIR}/hosts/premiere)

if(APPLE)
  enable_language(OBJCXX)
endif()
if(WIN32 AND NOT TARGET vv_aex_load_test)
  # LoadLibrary each .aex and check the entry points (no Adobe SDK needed at run time).
  add_executable(vv_aex_load_test ${VV_ENGINE_DIR}/tests/aex_load_test.cpp)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/vv_exports.cmake)

function(vv_add_premiere_effect target)
  cmake_parse_arguments(A "GPU_METAL;GPU_CUDA" "PIPL;BUNDLE_ID;METALLIB;METALLIB_TARGET" "SOURCES;LINK;INCLUDES" ${ARGN})
  set(srcs ${VV_PREMIERE_DIR}/vv_premiere_effect.cpp ${AE_SDK_UTIL}/AEFX_SuiteHelper.c ${A_SOURCES})
  if(A_GPU_METAL)
    list(APPEND srcs ${VV_PREMIERE_DIR}/vv_premiere_gpu_metal.mm)
  endif()
  if(A_GPU_CUDA)
    list(APPEND srcs ${VV_PREMIERE_DIR}/vv_premiere_gpu_cuda.cpp)
  endif()
  add_library(${target} MODULE ${srcs})
  target_link_libraries(${target} PRIVATE vv_core ${A_LINK})
  # Only the host entry points are exported (vv_exports.cmake).
  set(exports EffectMain PluginDataEntryFunction2)
  if(A_GPU_METAL OR A_GPU_CUDA)
    list(APPEND exports xGPUFilterEntry)
  endif()
  vv_export_only(${target} ${exports})
  target_include_directories(${target} PRIVATE ${A_INCLUDES} ${VV_PREMIERE_DIR}
    ${AE_SDK_HEADERS} ${AE_SDK_HEADERS}/SP ${AE_SDK_HEADERS}/adobesdk ${AE_SDK_UTIL}
    ${PR_SDK_HEADERS} ${PR_SDK_UTILS} ${PR_GPU_UTILS})
  set(gen ${CMAKE_CURRENT_BINARY_DIR}/${target}-pipl)
  file(MAKE_DIRECTORY ${gen})
  get_filename_component(pipl ${A_PIPL} ABSOLUTE)

  if(APPLE)
    target_link_libraries(${target} PRIVATE "-framework Foundation")
    if(A_GPU_METAL)
      target_link_libraries(${target} PRIVATE "-framework Metal")
    endif()
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    if(A_METALLIB)
      add_dependencies(${target} ${A_METALLIB_TARGET})
      target_sources(${target} PRIVATE ${A_METALLIB})
      set_source_files_properties(${A_METALLIB} PROPERTIES MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)
    endif()
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      MACOSX_BUNDLE_INFO_PLIST ${VV_PREMIERE_DIR}/Info.plist.in
      MACOSX_BUNDLE_GUI_IDENTIFIER ${A_BUNDLE_ID}
      MACOSX_BUNDLE_BUNDLE_NAME ${target})
    # PiPL: compile the .r into the bundle's resource file with Rez.
    set(rsrc ${gen}/${target}.rsrc)
    add_custom_command(OUTPUT ${rsrc}
      COMMAND xcrun Rez -o ${rsrc} -define __MACH__ -define AE_OS_MAC -useDF -arch arm64 -arch x86_64
              -i ${AE_SDK_HEADERS} -i ${AE_SDK_HEADERS}/SP -i ${AE_SDK_RESOURCES} -i ${VV_PREMIERE_DIR}
              ${pipl}
      DEPENDS ${pipl} ${VV_PREMIERE_DIR}/vv_pipl.r)
    target_sources(${target} PRIVATE ${rsrc})
    set_source_files_properties(${rsrc} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  else()
    # Windows: an AE-API effect is a DLL with the .aex extension. The PiPL is converted to a .rc
    # file the way the SDK samples do it: preprocess the .r, run the SDK's PiPLtool, preprocess
    # the result again with MSWindows defined, then compile the .rc into the DLL.
    set_target_properties(${target} PROPERTIES SUFFIX ".aex" PREFIX "")
    target_compile_definitions(${target} PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX)
    set(rr ${gen}/${target}.rr)
    set(rrc ${gen}/${target}.rrc)
    set(rc ${gen}/${target}_PiPL.rc)
    add_custom_command(OUTPUT ${rc}
      COMMAND cl /nologo /EP /P /Fi${rr} /TC
              /I${AE_SDK_HEADERS} /I${AE_SDK_HEADERS}/SP /I${AE_SDK_RESOURCES} /I${VV_PREMIERE_DIR} ${pipl}
      COMMAND ${AE_SDK_RESOURCES}/PiPLtool.exe ${rr} ${rrc}
      COMMAND cl /nologo /EP /P /Fi${rc} /DMSWindows /TC ${rrc}
      DEPENDS ${pipl} ${VV_PREMIERE_DIR}/vv_pipl.r
      WORKING_DIRECTORY ${gen}
      VERBATIM)
    target_sources(${target} PRIVATE ${rc})
  endif()
endfunction()
