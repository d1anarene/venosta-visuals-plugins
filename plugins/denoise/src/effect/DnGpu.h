// Denoise: GPU renderers for the shared host adapters (nullptr when not built) and the helpers the
// CPU and GPU paths share (noise estimate from a centred crop, parameter mapping).
#pragma once
#include <memory>

#include "DnParamsTable.h"
#include "dn_profile.h"
#include "dn_spatial.h"
#include "vv/effect.h"
#include "vv/image.h"

namespace dn {
/** Noise is estimated on a centred crop of at most this size (statistics, not detail, so a crop
 *  keeps the estimate cheap and identical between CPU and GPU paths). */
constexpr int kEstimateCropW = 1024, kEstimateCropH = 576;

vv::Image centreCrop(const vv::Image& img, int maxW = kEstimateCropW, int maxH = kEstimateCropH);
/** Single-frame starlet profile from an image that is already cropped, scaled by Noise Level. */
NoiseProfile estimateSpatialProfile(const vv::Image& crop, float level);
SpatialParams spatialParams(const EffectParams& ep);

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer();
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer();
}  // namespace dn
