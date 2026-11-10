#include <algorithm>
#include <cmath>
#include <vector>

#include "../blur/blur.h"
#include "../common/fe_kernels.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Multiscale diffusion: the soft-knee highlight light is blurred with Gaussians whose sigmas
// double each step up to `radius` (% of frame height). Each scale is a normalised blur, so
// total scattered energy is exactly intensity * highlight energy, distributed over a natural
// heavy-tailed profile (narrow core + wide skirt) instead of one flat halo.
Plane bloomEnergy(const Plane& lin, const BloomParams& p, Quality q) {
  const int w = lin.width, h = lin.height;
  Plane bright(w, h, 3);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    const float* c = &lin.v[i * 3];
    float wgt = fe_soft_knee_weight(fe_luma(fe_make3(c[0], c[1], c[2])), p.threshold, p.knee);
    for (int ch = 0; ch < 3; ++ch) bright.v[i * 3 + ch] = std::fmax(c[ch], 0.0f) * wgt;
  }
  const float sigmaMax = std::fmax(p.radius / 100.0f * h, 0.5f);
  const int scales = q == Quality::High ? 6 : 5;
  // weights grow towards wide scales (sum 1); finest scale keeps the core around light sources
  static const float kW5[5] = {0.10f, 0.15f, 0.20f, 0.25f, 0.30f};
  static const float kW6[6] = {0.08f, 0.12f, 0.15f, 0.18f, 0.22f, 0.25f};
  const float* weights = scales == 6 ? kW6 : kW5;

  // Pyramid evaluation: scale s is blurred at the coarsest level L where its sigma is still
  // >= ~2 level pixels, then all scales are accumulated coarse-to-fine with 2x upsampling.
  // Level L's prefilters (downsample [1 3 3 1] + bilinear upsample) already contribute about
  // 1.25 * (4^L - 1) / 3 px^2 of variance at full resolution, which is subtracted.
  std::vector<int> levelOf(scales);
  std::vector<float> sigmaAt(scales);
  int maxLevel = 0;
  for (int s = 0; s < scales; ++s) {
    float sigma = sigmaMax / std::pow(2.0f, static_cast<float>(scales - 1 - s));
    int L = 0;
    while (sigma / std::pow(2.0f, static_cast<float>(L + 1)) >= 2.0f && (h >> (L + 1)) >= 8 && (w >> (L + 1)) >= 8) ++L;
    float pre = 1.25f * (std::pow(4.0f, static_cast<float>(L)) - 1.0f) / 3.0f;
    levelOf[s] = L;
    sigmaAt[s] = std::sqrt(std::fmax(sigma * sigma - pre, 0.25f)) / std::pow(2.0f, static_cast<float>(L));
    maxLevel = std::max(maxLevel, L);
  }
  std::vector<Plane> levels;
  levels.push_back(std::move(bright));
  for (int L = 1; L <= maxLevel; ++L) levels.push_back(downsample2(levels.back()));

  Plane acc;
  for (int L = maxLevel; L >= 0; --L) {
    Plane here = acc.v.empty() ? Plane(levels[L].width, levels[L].height, 3)
                               : upsampleTo(acc, levels[L].width, levels[L].height);
    for (int s = 0; s < scales; ++s) {
      if (levelOf[s] != L) continue;
      Plane b = gaussianBlurDirect(levels[L], sigmaAt[s]);
      for (size_t i = 0; i < here.v.size(); ++i) here.v[i] += weights[s] * b.v[i];
    }
    acc = std::move(here);
  }
  for (float& v : acc.v) v *= p.intensity;
  return acc;
}

}  // namespace fe
