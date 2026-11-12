# vv_add_metallib(<target> SOURCE <file.metal> OUTPUT <file.metallib> [INCLUDES dirs...] [DEPENDS files...])
# Compiles a Metal shader (macOS) with -fno-fast-math (results stay close to the CPU reference)
# and the shared engine headers on the include path. Creates custom target <target>.
include_guard(GLOBAL)
function(vv_add_metallib target)
  cmake_parse_arguments(A "" "SOURCE;OUTPUT" "INCLUDES;DEPENDS" ${ARGN})
  get_filename_component(air ${A_OUTPUT} NAME_WE)
  get_filename_component(dir ${A_OUTPUT} DIRECTORY)
  set(incs -I ${VV_ENGINE_DIR}/core -I ${VV_ENGINE_DIR})
  foreach(i ${A_INCLUDES})
    list(APPEND incs -I ${i})
  endforeach()
  add_custom_command(OUTPUT ${A_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
    COMMAND xcrun -sdk macosx metal -fno-fast-math ${incs} -c ${A_SOURCE} -o ${dir}/${air}.air
    COMMAND xcrun -sdk macosx metallib ${dir}/${air}.air -o ${A_OUTPUT}
    DEPENDS ${A_SOURCE} ${A_DEPENDS} ${VV_ENGINE_DIR}/core/vv/kernels.h
    VERBATIM)
  add_custom_target(${target} DEPENDS ${A_OUTPUT})
endfunction()
