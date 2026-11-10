// Blur planning shared by the CPU reference and the GPU backends, so both make identical
// decisions (pyramid level, per-level sigma, scale weights).
#pragma once
#include <vector>

#include "../params/params.h"

namespace fe {

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

/** Number of bloom scales for a quality mode, and their weights (sum 1). */
int bloomScaleCount(Quality q);
const float* bloomScaleWeights(Quality q);

struct BloomPlan {
  std::vector<BlurStep> scales;  // fine -> wide
  std::vector<float> weights;
  int maxLevel = 0;
};
BloomPlan planBloom(const BloomParams& p, Quality q, int width, int height);

struct HalationPlan {
  BlurStep nearStep;  // sigma * 0.5
  BlurStep farStep;   // sigma (total), evaluated independently from the source
  float nearWeight = 0.6f;
  float farWeight = 0.4f;
};
HalationPlan planHalation(const HalationParams& p, int width, int height);

}  // namespace fe
