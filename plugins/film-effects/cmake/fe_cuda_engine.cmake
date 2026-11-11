# Defines the fe_cuda_engine static library (CUDA kernels + engine). Included from the top-level
# CMakeLists (FE_BUILD_CUDA_ENGINE) and from the Windows plugin build (FE_WITH_CUDA).
include_guard(GLOBAL)
enable_language(CUDA)
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "52")
  # Maxwell (sm_52) up to Ada, plus PTX so newer GPUs can JIT-compile.
  set(CMAKE_CUDA_ARCHITECTURES "52-real;61-real;75-real;86-real;89-real;89-virtual")
endif()
add_library(fe_cuda_engine STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/cuda/CudaFilmEngine.cu)
target_include_directories(fe_cuda_engine
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/cuda
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src)
target_link_libraries(fe_cuda_engine PUBLIC fe_core)
# --fmad=false keeps results closest to the CPU reference (no fused multiply-add contraction).
target_compile_options(fe_cuda_engine PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false>)
# The SDK guide recommends linking the CUDA runtime statically.
set_target_properties(fe_cuda_engine PROPERTIES CUDA_RUNTIME_LIBRARY Static POSITION_INDEPENDENT_CODE ON)
