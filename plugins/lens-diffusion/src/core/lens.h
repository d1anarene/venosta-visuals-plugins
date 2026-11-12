// Lens & Diffusion — CPU reference pipeline.
//
// Order: input -> linearise -> chromatic aberration (red/blue resampled radially, green fixed)
//   -> diffusion (energy-conserving scatter: out = lin + amount * (PSF * S - S), S = weighted light)
//   -> anamorphic streaks (horizontal-only blur of highlight light, tinted, added)
//   -> vignette (gain in linear light) -> encode -> global mix; alpha unchanged.
#pragma once
#include <cstdint>

#include "lens_params.h"
#include "vv/image.h"

namespace ld {

using vv::Image;
using vv::Plane;

/** Process `img` (straight-alpha RGBA float, top row first, working encoding) in place. Returns
 *  false for an empty image. With everything off the image is not touched at all. */
bool process(Image& img, const LensParams& params);

/** Point-spread function of the diffusion: three Gaussian scales (sigma in pixels) and weights. */
struct DiffusionPsf {
  float sigma[3];
  float weight[3];
};
DiffusionPsf diffusionPsf(const DiffusionParams& d, int height);

/** Anamorphic streak profile: three horizontal Gaussian scales (sigma in pixels) and weights. */
struct StreakProfile {
  float sigma[3];
  float weight[3];
};
StreakProfile streakProfile(const StreakParams& s, int width);

}  // namespace ld
