# Native Premiere plugin bundle (macOS). STATUS: not yet run — needs macOS, Xcode and the Adobe SDKs.
#
#   cmake -S . -B build-mac -G Xcode -DFE_BUILD_PLUGIN=ON \
#         -DAE_SDK_DIR="/path/to/AfterEffectsSDK/Examples" \
#         -DPR_SDK_DIR="/path/to/Premiere Pro SDK"
#
# The Adobe SDK file layout changes between releases; if a header is not found, point the cache
# variables below at the right folders (or start from the SDK_ProcAmp Xcode project and add our
# sources to it — see docs/INSTALLATION.md, "Build from the SDK sample").
if(NOT APPLE)
  message(FATAL_ERROR "FE_BUILD_PLUGIN currently targets macOS only")
endif()

set(AE_SDK_DIR "" CACHE PATH "After Effects SDK 'Examples' folder")
set(PR_SDK_DIR "" CACHE PATH "Premiere Pro SDK root folder")
set(AE_SDK_HEADERS "${AE_SDK_DIR}/Headers" CACHE PATH "")
set(AE_SDK_UTIL "${AE_SDK_DIR}/Util" CACHE PATH "")
set(AE_SDK_RESOURCES "${AE_SDK_DIR}/Resources" CACHE PATH "")
set(PR_SDK_HEADERS "${PR_SDK_DIR}/Examples/Headers" CACHE PATH "")
set(PR_GPU_UTILS "${PR_SDK_DIR}/Examples/Projects/GPUVideoFilter/Utils" CACHE PATH "Folder with PrGPUFilterModule.h")
option(FE_WITH_GPU "Build the Metal GPU entry point" ON)

enable_language(OBJCXX)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

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

# Premiere loads one effect (PiPL) per binary, so each variant is its own bundle built from the
# same sources: 0 = Film Effects (combined), 1 = Film Bloom, 2 = Film Halation, 3 = Film Grain.
set(FE_VARIANT_NAMES FilmEffects FilmBloom FilmHalation FilmGrain)
foreach(variant RANGE 3)
  list(GET FE_VARIANT_NAMES ${variant} bundle)
  add_library(${bundle} MODULE ${FE_PLUGIN_SOURCES})
  target_compile_definitions(${bundle} PRIVATE FE_VARIANT=${variant})
  target_link_libraries(${bundle} PRIVATE fe_core "-framework Foundation")
  if(FE_WITH_GPU)
    target_link_libraries(${bundle} PRIVATE "-framework Metal")
    target_compile_options(${bundle} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  endif()
  target_include_directories(${bundle} PRIVATE
    src/hosts/premiere src/gpu/metal src/core
    ${AE_SDK_HEADERS} ${AE_SDK_HEADERS}/SP ${AE_SDK_HEADERS}/adobesdk ${AE_SDK_UTIL}
    ${PR_SDK_HEADERS} ${PR_GPU_UTILS})
  set_target_properties(${bundle} PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/hosts/premiere/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.venostavisuals.${bundle}
    MACOSX_BUNDLE_BUNDLE_NAME ${bundle})

  # PiPL: compile the .r for this variant into the bundle's resource file with Rez.
  set(rsrc ${CMAKE_CURRENT_BINARY_DIR}/${bundle}/${bundle}.rsrc)
  add_custom_command(OUTPUT ${rsrc}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${bundle}
    COMMAND xcrun Rez -o ${rsrc} -define __MACH__ -define AE_OS_MAC -define FE_VARIANT=${variant} -useDF
            -arch arm64 -arch x86_64
            -i ${AE_SDK_HEADERS} -i ${AE_SDK_HEADERS}/SP -i ${AE_SDK_RESOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/hosts/premiere/FilmEffectsPiPL.r
    DEPENDS src/hosts/premiere/FilmEffectsPiPL.r)
  target_sources(${bundle} PRIVATE ${rsrc})
  set_source_files_properties(${rsrc} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  if(FE_WITH_GPU)
    target_sources(${bundle} PRIVATE ${FE_METALLIB})
    set_source_files_properties(${FE_METALLIB} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  endif()
endforeach()
