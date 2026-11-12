// Premiere Pro GPU entry point, CUDA (Windows / NVIDIA). See vv_premiere_gpu_entry.h. CUDA via the
// runtime API is supported when the runtime is linked statically (SDK guide, "Statically Link to
// CUDA Runtime"); the plugin's CUDA renderer does that.
#define VV_PR_GPU_FRAMEWORK PrGPUDeviceFramework_CUDA
#define VV_PR_GPU_API vv::GpuApi::Cuda
#include "vv_premiere_gpu_entry.h"
