#include "blur_plan.h"

#include <algorithm>
#include <cmath>

namespace fe {


int bloomScaleCount(Quality q) { return q == Quality::High ? 6 : 5; }

const float* bloomScaleWeights(Quality q) {
  // Weights grow towards wide scales (sum 1); the finest scale keeps a core around light sources.
  static const float kW5[5] = {0.10f, 0.15f, 0.20f, 0.25f, 0.30f};
  static const float kW6[6] = {0.08f, 0.12f, 0.15f, 0.18f, 0.22f, 0.25f};
  return q == Quality::High ? kW6 : kW5;
}

BloomPlan planBloom(const BloomParams& p, Quality q, int w, int h) {
  BloomPlan plan;
  const int n = bloomScaleCount(q);
  const float* wts = bloomScaleWeights(q);
  const float sigmaMax = std::max(p.radius / 100.0f * h, 0.5f);
  for (int s = 0; s < n; ++s) {
    float sigma = sigmaMax / std::pow(2.0f, static_cast<float>(n - 1 - s));
    BlurStep st = planGaussian(sigma, w, h);
    plan.scales.push_back(st);
    plan.weights.push_back(wts[s]);
    plan.maxLevel = std::max(plan.maxLevel, st.level);
  }
  return plan;
}

HalationPlan planHalation(const HalationParams& p, int w, int h) {
  HalationPlan plan;
  const float sigma = std::max(p.radius / 100.0f * h, 0.5f);
  plan.nearStep = planGaussian(sigma * 0.5f, w, h);
  plan.farStep = planGaussian(sigma, w, h);
  return plan;
}

}  // namespace fe
