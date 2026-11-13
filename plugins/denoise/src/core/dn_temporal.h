// Denoise — temporal candidate T1: motion-compensated, reliability-weighted temporal average,
// followed by the spatial stage on the (now less noisy) result.
//
//  1. Pilot: every frame of the window through the spatial filter (motion search only).
//  2. Motion per neighbour: coarse-to-fine block matching on pilot luma (3 levels, 8x8 blocks on a
//     4 px grid, integer search around predictors, parabolic sub-pixel refinement), bilinear dense
//     field. Vectors point from the centre frame into the neighbour.
//  3. Reliability per pixel: z = mean over a 5x5 window and RGB of (centre - warped neighbour)^2 /
//     expected noise variance of that difference (pixel noise from the profile, reduced by the
//     bilinear interpolation of the warped frame). z ~ 1 when aligned; occlusions, cuts and
//     misalignment give z >> 1. z is self-calibrated once per frame from the best-aligned
//     neighbour (median over its aligned pixels; robust to a profile that is somewhat off). Weight w = exp(-gain * max(z / g - slack, 0)).
//  4. Fusion: (centre + sum w * warped) / (1 + sum w); the noise reduction it achieved per pixel is
//     passed to the spatial stage as a noise scale map.
#pragma once
#include <vector>

#include "dn_profile.h"
#include "dn_spatial.h"
#include "vv/image.h"

namespace dn {

/** Pixel-domain noise std per RGB channel and value bin (same binning as NoiseProfile). */
struct PixelSigma {
  float sigma[3][kBins] = {};
  float at(int channel, float value) const;
};
/** Ground truth from a noisy/clean pair (RGB planes), binned by the clean value. */
PixelSigma measurePixelSigma(const vv::Plane& noisyRgb, const vv::Plane& cleanRgb);
/** From a profile estimated in Domain::Pixel (band 0). */
PixelSigma pixelSigmaFrom(const NoiseProfile& pixelDomainProfile);

struct TemporalParams {
  float slack = 1.25f;  // z below this counts as aligned
  float gain = 6.0f;    // how fast the weight falls above the slack
  int searchRadius = 3; // integer search per level (px at that level)
  float postStrength = 0.5f;  // spatial stage strength after fusion (detail vs residual noise)
};

struct TemporalDebug {
  float meanWeight = 0;  // mean neighbour weight (0..1), averaged over neighbours and pixels
  float zScale = 1;      // self-calibration factor of the reliability test (1 = profile exact)
};

/** frames: the window (any order of time, centre given by `centre`); all the same size.
 *  starletProfile drives the spatial stages; pixelSigma the reliability test. */
vv::Image denoiseTemporal(const std::vector<const vv::Image*>& frames, int centre, const NoiseProfile& starletProfile,
                          const PixelSigma& pixelSigma, const SpatialParams& spatial = {},
                          const TemporalParams& temporal = {}, TemporalDebug* debug = nullptr,
                          const std::vector<vv::Image>* pilots = nullptr);

/** Dense motion field (2 channels: dx, dy) from `from` to `to` (luma planes, 1 channel). */
vv::Plane estimateMotion(const vv::Plane& fromLuma, const vv::Plane& toLuma, int searchRadius);

}  // namespace dn
