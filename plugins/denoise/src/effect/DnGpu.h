// Denoise: GPU renderers for the shared host adapters (nullptr when not built) and the helpers the
// CPU and GPU paths share (noise estimate from a centred crop, parameter mapping).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "DnParamsTable.h"
#include "dn_profile.h"
#include "dn_spatial.h"
#include "vv/effect.h"
#include "vv/image.h"

namespace dn {
/** Noise is estimated on a centred crop of at most this size (statistics, not detail, so a crop
 *  keeps the estimate cheap and identical between CPU and GPU paths). */
constexpr int kEstimateCropW = 1024, kEstimateCropH = 576;
/** Quality Preview: smaller crop, about 9x cheaper; the estimate varies more from frame to frame
 *  (synthetic 1080p noise: 6.9 % instead of 1.7 % mean change of the band 0-2 sigmas). */
constexpr int kPreviewCropW = 384, kPreviewCropH = 216;
inline int cropW(const EffectParams& ep) { return ep.preview ? kPreviewCropW : kEstimateCropW; }
inline int cropH(const EffectParams& ep) { return ep.preview ? kPreviewCropH : kEstimateCropH; }

vv::Image centreCrop(const vv::Image& img, int maxW = kEstimateCropW, int maxH = kEstimateCropH);
/** Single-frame starlet profile from an image that is already cropped, scaled by Noise Level. */
NoiseProfile estimateSpatialProfile(const vv::Image& crop, float level);
/** The same estimate from per-band block statistics computed on the GPU, scaled by Noise Level. */
NoiseProfile profileFromBlocks(const std::vector<std::vector<EstimateBlock>>& blocks, float level);
SpatialParams spatialParams(const EffectParams& ep);

/** View "Timing (diagnostic)": the banner text and its size in pixels for a frame. */
struct Timing {
  const char* path;  // "CUDA", "METAL", "CPU"
  int width, height, cropW, cropH;
  bool half, preview;
  double estimateMs, filterMs, totalMs;
};
std::vector<std::string> timingLines(const Timing& t);
/** Draws the banner top left of `img` (top row first) at the scale for a frame of `frameHeight`. */
void drawTiming(vv::Image& img, const Timing& t, int frameHeight);
/** Banner size in pixels for a frame of this height, clipped to the frame. */
void timingBox(const Timing& t, int frameWidth, int frameHeight, int& w, int& h);
void flipRows(vv::Image& img);
/** Seconds since an arbitrary start, for the timing view. */
double nowMs();

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer();
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer();
}  // namespace dn
