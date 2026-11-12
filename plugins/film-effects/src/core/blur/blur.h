// Film Effects uses the shared engine's Gaussian blur (shared/engine/core/vv/blur.h).
#pragma once
#include "blur_plan.h"
#include "vv/blur.h"

namespace fe {
using vv::blurByPlan;
using vv::downsample2;
using vv::gaussianBlur;
using vv::gaussianBlurAccumulate;
using vv::gaussianBlurDirect;
using vv::gaussianKernel;
using vv::upsampleTo;
}  // namespace fe
