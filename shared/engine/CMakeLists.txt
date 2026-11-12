# Venosta Visuals shared engine: everything plugins have in common (images, threading, blur
# pyramid, pixel formats, per-pixel maths for CPU/CUDA/Metal). Plugins add it with
#   add_subdirectory(<repo>/shared/engine ${CMAKE_BINARY_DIR}/vv-engine)
# and link vv_core. See shared/engine/README.md.
include_guard(GLOBAL)
set(VV_ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "Shared engine root")

add_library(vv_core STATIC
  ${CMAKE_CURRENT_LIST_DIR}/core/vv/blur.cpp
  ${CMAKE_CURRENT_LIST_DIR}/core/vv/blur_plan.cpp
  ${CMAKE_CURRENT_LIST_DIR}/core/vv/pixel_io.cpp)
target_include_directories(vv_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/core)
target_compile_features(vv_core PUBLIC cxx_std_17)
# Plugins are shared libraries / bundles: always position independent.
set_target_properties(vv_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(vv_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
