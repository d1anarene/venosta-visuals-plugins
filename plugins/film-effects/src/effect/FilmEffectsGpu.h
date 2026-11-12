// Film Effects GPU renderers for the shared host adapters: Metal (macOS) and CUDA (Windows/Linux
// builds with the CUDA toolkit). Both run the engines that are compared with the CPU reference.
#pragma once
#include <memory>

#include "vv/effect.h"

namespace fe {
/** nullptr when this build has no such backend. */
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer();
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer();
}  // namespace fe
