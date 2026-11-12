// Film Effects blur plans (bloom scales, halation steps) on top of the shared engine planner.
#pragma once
#include <vector>

#include "../params/params.h"
#include "vv/blur_plan.h"

namespace fe {
using vv::BlurStep;
using vv::planGaussian;
using vv::pyramidVariance;

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
