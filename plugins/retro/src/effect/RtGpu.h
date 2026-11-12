// Retro GPU renderers for the shared host adapters (nullptr when not built).
#pragma once
#include <memory>

#include "vv/effect.h"

namespace rt {
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer();
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer();
}  // namespace rt
