// Blur planning shared by CPU and GPU backends, so both make identical decisions (pyramid level,
// per-level sigma).
#pragma once

namespace vv {

/** A Gaussian of `sigma` full-resolution pixels evaluated at pyramid level `level`
 *  (resolution / 2^level) with `sigmaAtLevel` level pixels, then upsampled `level` times. */
struct BlurStep {
  int level = 0;
  float sigmaAtLevel = 0.f;
};

/** Variance (full-res px^2) contributed by `level` rounds of [1 3 3 1] downsampling plus
 *  bilinear upsampling. */
float pyramidVariance(int level);

/** Coarsest level at which `sigma` is still >= 2 level pixels and the level is >= 8x8. */
BlurStep planGaussian(float sigma, int width, int height);

}  // namespace vv
