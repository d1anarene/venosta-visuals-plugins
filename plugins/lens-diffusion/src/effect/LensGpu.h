// Lens & Diffusion GPU renderers for the shared host adapters: Metal (macOS) and CUDA (builds with
// the CUDA toolkit). nullptr when this build has no such backend.
#pragma once
#include <memory>

#include "vv/effect.h"

namespace ld {
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer();
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer();
}  // namespace ld
