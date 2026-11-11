# Native Premiere plugins: Film Effects, Film Bloom, Film Halation, Film Grain.
# STATUS: not yet run — needs the Adobe SDKs (and Xcode on macOS / Visual Studio on Windows).
#
# macOS (CPU + Metal GPU):
#   cmake -S . -B build-mac -G Xcode -DFE_BUILD_PLUGIN=ON \
#         -DAE_SDK_DIR="/path/to/AfterEffectsSDK/Examples" -DPR_SDK_DIR="/path/to/Premiere Pro SDK"
# Windows (CPU + CUDA GPU; needs the CUDA toolkit for the GPU part, otherwise CPU only):
#   cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DFE_BUILD_PLUGIN=ON ^
#         -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples" -DPR_SDK_DIR="C:/Premiere Pro SDK"
#
# The Adobe SDK file layout changes between releases; if a header or tool is not found, point the
# cache variables below at the right folders (or start from the SDK_ProcAmp sample project and add
# our sources to it — see docs/INSTALLATION.md, "Build from the SDK sample").
if(NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "FE_BUILD_PLUGIN supports macOS and Windows only")
endif()

set(AE_SDK_DIR "" CACHE PATH "After Effects SDK 'Examples' folder")
set(PR_SDK_DIR "" CACHE PATH "Premiere Pro SDK root folder")
set(AE_SDK_HEADERS "${AE_SDK_DIR}/Headers" CACHE PATH "")
set(AE_SDK_UTIL "${AE_SDK_DIR}/Util" CACHE PATH "")
set(AE_SDK_RESOURCES "${AE_SDK_DIR}/Resources" CACHE PATH "Folder with AE_General.r (and PiPLtool.exe on Windows)")
set(PR_SDK_HEADERS "${PR_SDK_DIR}/Examples/Headers" CACHE PATH "")
set(PR_GPU_UTILS "${PR_SDK_DIR}/Examples/Projects/GPUVideoFilter/Utils" CACHE PATH "Folder with PrGPUFilterModule.h")

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
option(FE_WITH_CUDA "Build the CUDA GPU entry point (Windows, needs the CUDA toolkit)" ${FE_CUDA_DEFAULT})
if(FE_WITH_CUDA)
  if(NOT WIN32)
    message(FATAL_ERROR "FE_WITH_CUDA targets Windows")
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fe_cuda_engine.cmake)
  message(STATUS "Film Effects: CUDA GPU path enabled")
elseif(WIN32)
  message(STATUS "Film Effects: no CUDA toolkit found - Windows plugins will render on the CPU only")
endif()

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(FE_PLUGIN_SOURCES
  src/hosts/premiere/FilmEffects.cpp
  ${AE_SDK_UTIL}/AEFX_SuiteHelper.c)
if(FE_WITH_GPU)
  list(APPEND FE_PLUGIN_SOURCES src/hosts/premiere/FilmEffectsGPU.mm src/gpu/metal/MetalFilmEngine.mm)
  set(FE_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/FilmEffects.metallib)
  add_custom_command(OUTPUT ${FE_METALLIB}
    COMMAND xcrun -sdk macosx metal -fno-fast-math -I ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            -c ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/FilmEffects.metal -o FilmEffects.air
    COMMAND xcrun -sdk macosx metallib FilmEffects.air -o ${FE_METALLIB}
    DEPENDS src/gpu/metal/FilmEffects.metal src/core/common/fe_kernels.h)
endif()

set(FE_PIPL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/hosts/premiere/FilmEffectsPiPL.r)

# Premiere loads one effect (PiPL) per binary, so each variant is its own plugin built from the
# same sources: 0 = Film Effects (combined), 1 = Film Bloom, 2 = Film Halation, 3 = Film Grain.
set(FE_VARIANT_NAMES FilmEffects FilmBloom FilmHalation FilmGrain)
foreach(variant RANGE 3)
  list(GET FE_VARIANT_NAMES ${variant} bundle)
  set(gen ${CMAKE_CURRENT_BINARY_DIR}/${bundle})
  file(MAKE_DIRECTORY ${gen})

  add_library(${bundle} MODULE ${FE_PLUGIN_SOURCES})
  target_compile_definitions(${bundle} PRIVATE FE_VARIANT=${variant})
  target_link_libraries(${bundle} PRIVATE fe_core)
  target_include_directories(${bundle} PRIVATE
    src/hosts/premiere src/gpu/metal src/core
    ${AE_SDK_HEADERS} ${AE_SDK_HEADERS}/SP ${AE_SDK_HEADERS}/adobesdk ${AE_SDK_UTIL}
    ${PR_SDK_HEADERS} ${PR_GPU_UTILS})

  if(APPLE)
    target_link_libraries(${bundle} PRIVATE "-framework Foundation")
    if(FE_WITH_GPU)
      target_link_libraries(${bundle} PRIVATE "-framework Metal")
      target_compile_options(${bundle} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
      target_sources(${bundle} PRIVATE ${FE_METALLIB})
      set_source_files_properties(${FE_METALLIB} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
    set_target_properties(${bundle} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/hosts/premiere/Info.plist.in
      MACOSX_BUNDLE_GUI_IDENTIFIER com.venostavisuals.${bundle}
      MACOSX_BUNDLE_BUNDLE_NAME ${bundle})
    # PiPL: compile the .r for this variant into the bundle's resource file with Rez.
    set(rsrc ${gen}/${bundle}.rsrc)
    add_custom_command(OUTPUT ${rsrc}
      COMMAND xcrun Rez -o ${rsrc} -define __MACH__ -define AE_OS_MAC -define FE_VARIANT=${variant} -useDF
              -arch arm64 -arch x86_64
              -i ${AE_SDK_HEADERS} -i ${AE_SDK_HEADERS}/SP -i ${AE_SDK_RESOURCES}
              ${FE_PIPL_SRC}
      DEPENDS ${FE_PIPL_SRC})
    target_sources(${bundle} PRIVATE ${rsrc})
    set_source_files_properties(${rsrc} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  else()
    # Windows: an AE-API effect is a DLL with the .aex extension. The PiPL is converted to a .rc
    # file the way the SDK samples do it: preprocess the .r, run the SDK's PiPLtool, preprocess
    # the result again with MSWindows defined, then compile the .rc into the DLL.
    set_target_properties(${bundle} PROPERTIES SUFFIX ".aex" PREFIX "")
    if(FE_WITH_CUDA)
      target_sources(${bundle} PRIVATE src/hosts/premiere/FilmEffectsGPU_CUDA.cpp)
      target_include_directories(${bundle} PRIVATE src/gpu/cuda)
      target_link_libraries(${bundle} PRIVATE fe_cuda_engine)
    endif()
    target_compile_definitions(${bundle} PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX)
    set(rr ${gen}/${bundle}.rr)
    set(rrc ${gen}/${bundle}.rrc)
    set(rc ${gen}/${bundle}_PiPL.rc)
    add_custom_command(OUTPUT ${rc}
      COMMAND cl /nologo /EP /P /Fi${rr} /TC /DFE_VARIANT=${variant}
              /I${AE_SDK_HEADERS} /I${AE_SDK_HEADERS}/SP /I${AE_SDK_RESOURCES} ${FE_PIPL_SRC}
      COMMAND ${AE_SDK_RESOURCES}/PiPLtool.exe ${rr} ${rrc}
      COMMAND cl /nologo /EP /P /Fi${rc} /DMSWindows /TC ${rrc}
      DEPENDS ${FE_PIPL_SRC}
      WORKING_DIRECTORY ${gen}
      VERBATIM)
    target_sources(${bundle} PRIVATE ${rc})
  endif()
endforeach()
