# rt_cuda_engine: Retro CUDA passes (static library). Needs the CUDA toolkit.
include_guard(GLOBAL)
enable_language(CUDA)
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "52")
  set(CMAKE_CUDA_ARCHITECTURES "61-real;75-real;86-real;89-real;89-virtual")
endif()
add_library(rt_cuda_engine STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/cuda/RetroCuda.cu)
target_include_directories(rt_cuda_engine PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/cuda
                                          PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/common)
target_link_libraries(rt_cuda_engine PUBLIC rt_core)
target_compile_options(rt_cuda_engine PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false>)
set_target_properties(rt_cuda_engine PROPERTIES CUDA_RUNTIME_LIBRARY Static POSITION_INDEPENDENT_CODE ON)
